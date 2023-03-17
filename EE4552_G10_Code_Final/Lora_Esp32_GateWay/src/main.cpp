/*

Gateway

      LoRa SX1278           Module ESP32
      3.3V                  3.3V
      NSS                   D5
      MOSI                  D23
      DIO0                  D2
      GND                   GND
      RST                     D14
      SCK                 D18
      MISO                  D19 


Gateway
- Nhận dữ liêu nhiệt độ đo được từ EndNode và hiển thị lên ThingSpeak, xuất file excel
+ Kiểm tra xem dữ liệu có phải được gửi từ 10 địa chỉ Endnode đã cấp trước không, nếu đúng thì mới nhận, còn không thì không nhận
+ Kiểm tra địa chỉ xem có phải gửi cho Gateway hoặc Broadcast không
+ Kiểm tra xem có phải chỉ số bản tin mới không ( tránh trường hợp tấn công bản tin gây treo hệ thống ).
=> Các điều kiện kiểm tra đúng thì mới nhận gói tin.
+ Có dữ liệu nhiệt độ mới thì đẩy lên ThingSpeak để hiển thị và xuất file Excel.

- Tạo Web Server để cập nhật ngưỡng nhiệt
+ Có hiển thị nhiệt độ mới nhận được từ Endnode
+ Nhận dữ liệu cập nhật ngưỡng từ Web Server,khi ngưỡng nhiệt độ thay đổi thì mới gửi xuống EndNode để thay đổi ngưỡng.
+ Dữ liệu truyền đi dạng abcdef: abc = temp_blue, def = temp_yellow
Giả sử nhiệt độ đo được là temp_current
temp_current <= temp_blue:  bật đèn xanh
temp_blue < temp_current < temp_yellow: bật đèn vàng
temp_current >= temp_yellow: bật đèn đỏ

*/


// include libraries
#include <SPI.h>              
#include <SPI.h>              // include libraries
#include <LoRa.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// Khai báo chân Lora
#define ss 5
#define rst 14
#define dio0 2

// Khai bao cac bien trong thinkspeak 
String apiKey = "F7AZ9ET9O00GJVRK"; // Enter your Write API key from ThingSpeak
const char* server1 = "api.thingspeak.com";

// Khai báo wifi
const char* ssid = "268 Le Trong Tan.";
const char* password = "abcd1234";
//khai bao bien cho webserver cong 81
const char* PARAM_INPUT = "input";
String inputParam;
WiFiClient client;
AsyncWebServer server(81);

//html cho webserver
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
    <title>Control</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">

    </head>
    <style>body { min-width: 310px;max-width: 800px;height: 400px;margin: 0 auto;text-align: center;font-family: Arial;font-size: 1.5rem;}
    h1 {font-family: Arial;font-size: 2.5rem;text-align: center;color:Tomato }
    p {font-size: 2.5rem;}
    </style>
    <body>
    <h1>Update Temperature Thresholds</h1>
    <h2>Update</h2>
    <p>
      <i class="fas fa-thermometer-half" style="color:#e23004;"></i> 
      <span class="labels">Temperature</span> 
      <span id="temperature">%TEMPERATURE%</span>
      <sup class="units">&deg;C</sup>
    </p>
    <form action="/get">
        Temperature: <input type="text" name="input">
      <input type="submit" value="Submit">
    </form><br>
    
    <script>

      setInterval(function ( ) {
        var xhttp = new XMLHttpRequest();
        xhttp.onreadystatechange = function() {
          if (this.readyState == 4 && this.status == 200) {
            document.getElementById("temperature").innerHTML = this.responseText;
          }
        };
        xhttp.open("GET", "/temperature", true);
        xhttp.send();
      }, 10000 ) ;
      </script>
  </body></html>)rawliteral";
  //hàm kiểm tra kết nối webserver
  void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
  }

// Khai báo gói tin 
byte msgCount = 0;            // chỉ số bản tin gửi đi
byte localAddress = 0x01;     // địa chỉ của Gateway này
byte destination = 0xFF;      // gửi broadcast đến Endnode

// Chuỗi chứa dữ liệu nhận được từ Web Server
String inputMessage;          
String pre_outgoing;          // lưu lại dữ liệu nhận được lần gần nhất


// Biến chứa nhiệt độ nhận được từ gateway
float temperature = 0;  
int pre_incomingMsgId = 0;   // chỉ số bản tin nhận trước đó
      
int payloadLenght = 4;       // Độ dài bản tin
byte Data[4];                // Biến chứa dữ liệu gửi đi

String Temperature(){
return String(temperature);}

void setup() 
{
  Serial.begin(115200);
  while (!Serial);
  Serial.println("LoRa Gateway");
  // Cài đặt Lora
  LoRa.setPins(ss, rst, dio0);    
 
  while (!LoRa.begin(433E6))     //433E6 - Asia, 866E6 - Europe, 915E6 - North America
  {
    Serial.println(".");
    delay(500);
  }
  LoRa.setSyncWord(0xA5);
  Serial.println("LoRa Initializing OK!");

  inputMessage = "check";
  pre_outgoing = inputMessage;

  // kết nối wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed!");
    return;
  }
  Serial.println();
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  // tạo Webserver
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
   server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", Temperature().c_str());
  });
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if (request->hasParam(PARAM_INPUT)) {
      inputMessage = request->getParam(PARAM_INPUT)->value();
      inputParam = PARAM_INPUT;
    }
    else {
      inputParam = "none";
    }
    Serial.println(inputParam );
    Serial.println("Chuoi gui di");
    Serial.println(inputMessage);
    request->send(200, "text/html", "HTTP GET request sent to your ESP on input field (" 
                                     + inputParam + ") with value: " + inputMessage +
                                     "<br><a href=\"/\">Return to Home Page</a>");
  });
  server.onNotFound(notFound);
  server.begin();

}

// Gửi dữ liệu xuống EndNode
void sendMessage(byte* outgoing) {
  LoRa.beginPacket();                   // bắt đầu gói tin
  LoRa.write(destination);              // thêm địa chỉ nhận
  LoRa.write(localAddress);             // thêm địa chỉ gửi
  LoRa.write(msgCount);                 // thêm chỉ số bản tin gửi đi     
  LoRa.write(payloadLenght);            // thêm độ dài bản tin gửi đi
  LoRa.write(outgoing, payloadLenght);  // thêm bản tin
  LoRa.endPacket();                     // kết thúc gói tin và gửi đi
  msgCount++;                           // tăng chỉ số bản tin
}

//Nhan du lieu tu End Node
void onReceive(int packetSize) {
  
  if (packetSize == 0) return;          // Nếu không có bản tin thì return

  // read packet header bytes:
  int recipient = LoRa.read();          // địa chỉ người nhận
  byte sender = LoRa.read();            // địa chỉ người gửi
  byte incomingMsgId = LoRa.read();     // chỉ số bản tin nhận
  byte incomingLength = LoRa.read();    // độ dài bản tin nhận

  Serial.println("Received from: 0x" + String(sender, HEX));
  Serial.println("Sent to: 0x" + String(recipient, HEX));
  Serial.println("Message ID: " + String(incomingMsgId));
  Serial.println("Message length: " + String(incomingLength));
  Serial.println();


  byte incomingData[4];
  int i=0;

// Data từ EndNode (10 Endnode)
// Kiểm tra xem dữ liệu có phải được gửi từ 10 địa chỉ này không, có phải là chỉ số bản tin mới không  
// Nếu đúng thì mới nhận, còn không thì không nhận
// Kiểm tra địa chỉ
  if ( sender == 0x2 || sender == 0x3 || sender == 0x4 || sender == 0x5 || sender == 0x6 
       || sender == 0x7 || sender == 0x8 || sender == 0x9 || sender == 0x10 || sender == 0x11 ) {                  
// Kiểm tra chỉ số bản tin
    if(pre_incomingMsgId < incomingMsgId)
    {
    pre_incomingMsgId = incomingMsgId;
    Serial.println("In the case of data receiving");    
    while (LoRa.available()) {
    incomingData[i] = LoRa.read();
    i++;
    } 
    }   
  }
  
// Nếu dữ liệu không phải cho Gateway hoăc Broadcast thì sẽ không nhận
  if (recipient != localAddress && recipient != 0xFF) {   // check xem co phai gui cho minh k : 0xFF: broadcast
    Serial.println("This message is not for me.");
    return;                             // skip rest of function
  }

// Demo data from End Node 0x2
  if (sender == 0x2) {                                                                               
    temperature = ((incomingData[0] << 8) | incomingData[1]);
    temperature = temperature/10;
    Serial.println("Nhiet do la");
    Serial.println(temperature);
    // gửi nhiệt độ lên thinkspeak
  if (client.connect(server1, 80)) // "184.106.153.149" or api.thingspeak.com
   {
      String postStr = apiKey;
      postStr += "&field1=";
      postStr += String(temperature);
      postStr += "\r\n\r\n\r\n\r\n";
    
      client.print("POST /update HTTP/1.1\n");
      client.print("Host: api.thingspeak.com\n");
      client.print("Connection: close\n");
      client.print("X-THINGSPEAKAPIKEY: " + apiKey + "\n");
      client.print("Content-Type: application/x-www-form-urlencoded\n");
      client.print("Content-Length: ");
      client.print(postStr.length());
      client.print("\n\n");
      client.print(postStr);
      //client.stop();
 
    }    
  }
}
// Hàm nhận ngưỡng nhiệt và gửi xuống Endnode
void temperature_threshold()
{
// Chuỗi nhận được là string inputMessage ="abcdec" abc:T_blue, def: T_yellow
// Kiểm tra xem có dữ liệu mới từ Web Server không, nếu có thì mới gửi, không thì thoát
  if(inputMessage.equals(pre_outgoing)) return;
  else
  {
  pre_outgoing = inputMessage;
  int result = inputMessage.toInt (); 
  int temp_blue = result/1000;
  int temp_yellow = result%1000;
  Data[0] = temp_blue >> 8; 
  Data[1] = temp_blue;
  Data[2] = temp_yellow >> 8; 
  Data[3] = temp_yellow;
  sendMessage(Data);
  }
}
void loop() {
  
  onReceive(LoRa.parsePacket()); 
  temperature_threshold();
  
}

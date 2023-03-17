/*

 
			LoRa SX1278           Module ESP32
			3.3V	                3.3V
			NSS                  	D5
			MOSI	                D23
			DIO0                 	D2
			GND                 	GND
			RST                     D14
			SCK	                D18
			MISO	                D19 


// Chân dữ liệu DS18B20
const int oneWireBus = 13; 
// Chân nút nhấn
#define btnPin 15 
// Các ngưỡng Led                
#define led_blue 25               
#define led_yellow 26
#define led_red 27

*** EndNode
 Chủ yếu ở chế độ chỉ gửi dữ liệu, thời gian thức khoảng 2s
- Giữ nút 1 lúc > 1s thì vào chế độ: Nhận dữ liệu từ Gateway để cập nhật ngưỡng nhiệt độ
+ kiểm tra địa chỉ xem có phải gửi cho EndNode này hoặc Broadcast không, đúng mới nhận, còn không thì không nhận.
+ Khi nhận được gói tin sẽ tiến hành phân tích và cập nhật ngưỡng nhiệt mới.
+ Dữ liệu truyền đi dạng abcdef: abc = T_Blue, def = T_Yellow
  Giả sử nhiệt độ đo được là temp_current
  Temp_current <= T_Blue:  bật đèn xanh
  T_Blue < Temp_current <= T_Yellow: bật đèn vàng
  Temp_current > T_Yellow: bật đèn đỏ
+ Thả nút sẽ thoát khỏi chế độ nhận dữ liệu, đo và hiển thị Led xem có đúng với ngưỡng cập nhật không, rồi lại vào chế độ ngủ.
- Khi nhấn nút < 1s: bắt đầu đo nhiệt độ và gửi dữ liệu lên Gateway, gửi xong vào chế độ ngủ
+ Hiển thị Led theo ngưỡng nhiệt trong 0.5s.

*/


// Include libraries
#include <Arduino.h>
#include <SPI.h>              
#include <LoRa.h>
#include <OneWire.h> 
#include <DallasTemperature.h>

// Khai báo chân Lora
#define ss 5
#define rst 14
#define dio0 2

// Cài đặt các chân
#define btnPin 15             // chân nhận tín hiệu Nút nhấn
#define led_blue 25           // chân điều khiển Led xanh
#define led_yellow 26         // chân điều khiển Led vàng
#define led_red 27            // chân điều khiển Led đỏ
const int oneWireBus = 13;    // Chân nhận dữ liệu từ cảm biến


// Khai báo gói tin 
RTC_DATA_ATTR byte msgCount = 1;            // chỉ số bản tin gửi đi
byte localAddress = 0x02;                   // địa chỉ của Endnode này
byte destination = 0x01;                    // địa chỉ của Gateway

// Nhiệt độ
float temperature = 0;                        // Biến chứa dữ liệu nhiệt độ
RTC_DATA_ATTR int T_Blue = 25;              // Ngưỡng nhiệt độ đèn xanh
RTC_DATA_ATTR int T_Yellow = 30;            // Ngưỡng nhiệt độ đèn vàng

// Bản tin gửi đi
int payloadLenght = 4;                      // Độ dài bản tin
byte Data[4];                               // Biến chứa dữ liệu gửi đi

// Cài đặt cảm biến
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

void setup() 
{
//Cài đặt chân 15 để làm nút nhấn
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_15,1); //1 = High, 0 = Low
// Cài đặt các chân để điều khiển Led
  pinMode(led_blue, OUTPUT); 
  pinMode(led_yellow, OUTPUT); 
  pinMode(led_red, OUTPUT); 	
// Cài đặt Serial
  Serial.begin(115200);
  while (!Serial);
  Serial.println("Wake up");
  Serial.println("LoRa Endnode 0x02");
// Cài đặt Lora
  LoRa.setPins(ss, rst, dio0);    
  while (!LoRa.begin(433E6))     //433E6 - Asia, 866E6 - Europe, 915E6 - North America
  {
    Serial.println(".");
    delay(500);
  }
  LoRa.setSyncWord(0xA5);
  Serial.println("LoRa Initializing OK!");
// Cài đặt cảm biến
  sensors.begin(); 
  delay (1000);
}

// Hàm gửi dữ liệu lên Gateway
void sendMessage(byte* outgoing) {
  LoRa.beginPacket();                                            // bắt đầu gói tin
  LoRa.write(destination);                                       // thêm địa chỉ nhận
  LoRa.write(localAddress);                                      // thêm địa chỉ gửi
  LoRa.write(msgCount);                                          // thêm chỉ số bản tin gửi đi      
  LoRa.write(payloadLenght);                                     // thêm độ dài bản tin gửi đi
  LoRa.write(outgoing, payloadLenght);                           // thêm bản tin
  LoRa.endPacket();                                              // kết thúc gói tin và gửi đi
  Serial.println("Packet index: " + String(msgCount));           // in chỉ số của gói tin
  msgCount++;                                                    // tăng chỉ số bản tin
}



// Hàm nhận dữ liệu để thay đổi ngưỡng nhiệt
void onReceive(int packetSize) {
  if (packetSize == 0) return;          // Nếu không có bản tin thì return
//Phân tích dữ liệu nhận được
  int recipient = LoRa.read();          // địa chỉ người nhận
  byte sender = LoRa.read();            // địa chỉ người gửi
  byte incomingMsgId = LoRa.read();     // chỉ số bản tin nhận
  byte incomingLength = LoRa.read();    // độ dài bản tin nhận

  Serial.println("Received from: 0x" + String(sender, HEX));
  Serial.println("Sent to: 0x" + String(recipient, HEX));
  Serial.println("Message ID: " + String(incomingMsgId));
  Serial.println("Message length: " + String(incomingLength));
  byte incomingData[4];
  int i=0;
  Serial.println("We are in the case of data receiving");    
  while (LoRa.available()) {
  incomingData[i] = LoRa.read();
  i++;
  }    
// Kiểm tra xem nếu gửi cho Endnode này hoặc Broadcast thì mới nhận
  if (recipient != localAddress && recipient != 0xFF) {   
    Serial.println("This message is not for me.");
    return;                             // skip rest of function
  }
// Nhận ngưỡng nhiệt
    T_Blue = ((incomingData[0] << 8) | incomingData[1]);
    T_Blue = T_Blue;
    T_Yellow = ((incomingData[2] << 8) | incomingData[3]);
    T_Yellow = T_Yellow;
    Serial.println("Blue Temperature Threshold: " + String (T_Blue));
    Serial.println("Yellow Temperature Threshold: " + String (T_Yellow));
  }

void loop() {
  int read = 0;
 // Nếu vẫn đang nhấn nút thì vào chế độ nhận ngưỡng nhiệt
  while(digitalRead(btnPin) == HIGH)
  {
  Serial.println("In the case of receiving heat threshold");
	onReceive(LoRa.parsePacket()); 
  }	

// Cảm biến đo nhiệt độ lưu vào biến Temperature
  sensors.requestTemperatures();  
  temperature = sensors.getTempCByIndex(0);
// Chuyen kieu int; *10 để gửi đi
  int Temperature = int (temperature*10); 
  Serial.print("Temperature is: "); 
  Serial.println(temperature);
// Hiển thị Led theo nhiệt độ
  if(temperature<=T_Blue)
  {
  digitalWrite(led_blue,HIGH);
  digitalWrite(led_yellow,LOW);
  digitalWrite(led_red,LOW);
  }
  else if(temperature<T_Yellow)
  {
  digitalWrite(led_blue,LOW);
  digitalWrite(led_yellow,HIGH);
  digitalWrite(led_red,LOW);
  }
  else
  {
  digitalWrite(led_blue,LOW);
  digitalWrite(led_yellow,LOW);
  digitalWrite(led_red,HIGH);
  }
// Serial để nhìn
  read = digitalRead(led_blue);
  Serial.println("Led_blue: " + String (read)); 
  read = digitalRead(led_yellow);
  Serial.println("Led_yellow: "+ String (read)); 
  read = digitalRead(led_red);
  Serial.println("Led_red: "+ String (read));

// Tạo gói tin và gửi dữ liệu đến Gateway
  Data[0] = Temperature >> 8; 
  Data[1] = Temperature;
  Serial.println("Data sent: "+ String(Temperature));
  sendMessage(Data);

// Để Led chỉ thị ngưỡng sáng trong 0.5s
  delay (500);

// Vào chế độ ngủ
  Serial.println("Going to sleep now");
  esp_deep_sleep_start();
  }

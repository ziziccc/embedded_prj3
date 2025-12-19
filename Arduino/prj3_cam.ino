#include <ArduCAM.h>
#include <Wire.h>
#include <ArduCAM.h>
#include <SPI.h>

#define SAMPLE_N 256        
#define SAME_RATIO_PCT 3    
#define LEN_TOL 32          

static uint8_t prev_sample[SAMPLE_N];
static uint32_t prev_len = 0;
static bool prev_valid = false;
#define BMPIMAGEOFFSET 66
const char bmp_header[BMPIMAGEOFFSET] PROGMEM =
{
  0x42, 0x4D, 0x36, 0x58, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x28, 0x00,
  0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x10, 0x00, 0x03, 0x00,
  0x00, 0x00, 0x00, 0x58, 0x02, 0x00, 0xC4, 0x0E, 0x00, 0x00, 0xC4, 0x0E, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x00, 0x00, 0xE0, 0x07, 0x00, 0x00, 0x1F, 0x00,
  0x00, 0x00
};
const int CS = 7;
bool is_header = false;
int mode = 0;
uint8_t start_capture = 0;
 ArduCAM myCAM( OV5642, CS );
uint8_t read_fifo_burst(ArduCAM myCAM);
void setup() {
uint8_t vid, pid;
uint8_t temp;
#if defined(__SAM3X8E__)
  Wire1.begin();
  Serial.begin(115200);
#else
  Wire.begin();
  Serial.begin(256000);
#endif
Serial.println(F("ACK CMD ArduCAM Start! END"));
pinMode(CS, OUTPUT);
digitalWrite(CS, HIGH);
SPI.begin();
myCAM.write_reg(0x07, 0x80);
delay(100);
myCAM.write_reg(0x07, 0x00);
delay(100); 
while(1){
  myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
  temp = myCAM.read_reg(ARDUCHIP_TEST1);
  if (temp != 0x55){
    Serial.println(F("ACK CMD SPI interface Error! END"));
    delay(1000);continue;
  }else{
    Serial.println(F("ACK CMD SPI interface OK. END"));break;
  }
}
  while(1){
    myCAM.wrSensorReg16_8(0xff, 0x01);
    myCAM.rdSensorReg16_8(OV5642_CHIPID_HIGH, &vid);
    myCAM.rdSensorReg16_8(OV5642_CHIPID_LOW, &pid);
    if((vid != 0x56) || (pid != 0x42)){
      Serial.println(F("ACK CMD Can't find OV5642 module! END"));
      delay(1000);continue;
    }
    else{
      Serial.println(F("ACK CMD OV5642 detected. END"));break;
    } 
  }

myCAM.set_format(JPEG);
myCAM.InitCAM();

  myCAM.write_reg(ARDUCHIP_TIM, VSYNC_LEVEL_MASK);   
  myCAM.OV5642_set_JPEG_size(OV5642_320x240);
delay(1000);
myCAM.clear_fifo_flag();
myCAM.write_reg(ARDUCHIP_FRAMES,0x00);
}
void loop() {
uint8_t temp = 0xff, temp_last = 0;
bool is_header = false;
if (Serial.available())
{
  temp = Serial.read();
  switch (temp)
  {
    case 0:
      myCAM.OV5642_set_JPEG_size(OV5642_320x240);delay(1000);
      Serial.println(F("ACK CMD switch to OV5642_320x240 END"));
    temp = 0xff;
    break;
    case 1:
      myCAM.OV5642_set_JPEG_size(OV5642_640x480);delay(1000);
      Serial.println(F("ACK CMD switch to OV5642_640x480 END"));
    temp = 0xff;
    break;
    case 2: 
      myCAM.OV5642_set_JPEG_size(OV5642_1024x768);delay(1000);
      Serial.println(F("ACK CMD switch to OV5642_1024x768 END"));
    temp = 0xff;
    break;
    case 3:
    temp = 0xff;
      myCAM.OV5642_set_JPEG_size(OV5642_1280x960);delay(1000);
      Serial.println(F("ACK CMD switch to OV5642_1280x960 END"));
    break;
    case 4:
    temp = 0xff;
      myCAM.OV5642_set_JPEG_size(OV5642_1600x1200);delay(1000);
      Serial.println(F("ACK CMD switch to OV5642_1600x1200 END"));
    break;
    case 5:
    temp = 0xff;
      myCAM.OV5642_set_JPEG_size(OV5642_2048x1536);delay(1000);
      Serial.println(F("ACK CMD switch to OV5642_2048x1536 END"));
    break;
    case 6:
    temp = 0xff;
      myCAM.OV5642_set_JPEG_size(OV5642_2592x1944);delay(1000);
      Serial.println(F("ACK CMD switch to OV5642_2592x1944 END"));
    break;
    case 0x10:
    mode = 1;
    temp = 0xff;
    start_capture = 1;
    Serial.println(F("ACK CMD CAM start single shoot. END"));
    break;
    case 0x11: 
    temp = 0xff;
    myCAM.set_format(JPEG);
    myCAM.InitCAM();
    #if !(defined (OV2640_MINI_2MP))
    myCAM.set_bit(ARDUCHIP_TIM, VSYNC_LEVEL_MASK);
    #endif
    break;
    case 0x20:
    mode = 2;
    temp = 0xff;
    start_capture = 2;
    Serial.println(F("ACK CMD CAM start video streaming. END"));
    break;
    case 0x30:
    mode = 3;
    temp = 0xff;
    start_capture = 3;
    Serial.println(F("ACK CMD CAM start single shoot. END"));
    break;
    case 0x31:
    temp = 0xff;
    myCAM.set_format(BMP);
    myCAM.InitCAM();     
    myCAM.clear_bit(ARDUCHIP_TIM, VSYNC_LEVEL_MASK);
    myCAM.wrSensorReg16_8(0x3818, 0x81);
    myCAM.wrSensorReg16_8(0x3621, 0xA7);
    break;
    case 0x40:
    myCAM.OV5642_set_Light_Mode(Advanced_AWB);temp = 0xff;
     Serial.println(F("ACK CMD Set to Advanced_AWB END"));break;
    case 0x41:
    myCAM.OV5642_set_Light_Mode(Simple_AWB);temp = 0xff;
     Serial.println(F("ACK CMD Set to Simple_AWB END"));break;
     case 0x42:
    myCAM.OV5642_set_Light_Mode(Manual_day);temp = 0xff;
     Serial.println(F("ACK CMD Set to Manual_day END"));break;
     case 0x43:
    myCAM.OV5642_set_Light_Mode(Manual_A);temp = 0xff;
     Serial.println(F("ACK CMD Set to Manual_A END"));break;
     case 0x44:
    myCAM.OV5642_set_Light_Mode(Manual_cwf);temp = 0xff;
     Serial.println(F("ACK CMD Set to Manual_cwf END"));break;
     case 0x45:
    myCAM.OV5642_set_Light_Mode(Manual_cloudy);temp = 0xff;
     Serial.println(F("ACK CMD Set to Manual_cloudy END"));break;
      case 0x50:
    myCAM.OV5642_set_Color_Saturation(Saturation4);temp = 0xff;
     Serial.println(F("ACK CMD Set to Saturation+4 END"));break;
   case 0x51:
      myCAM.OV5642_set_Color_Saturation(Saturation3);temp = 0xff;
     Serial.println(F("ACK CMD Set to Saturation+3 END"));break;
   case 0x52:
    myCAM.OV5642_set_Color_Saturation(Saturation2);temp = 0xff;
   Serial.println(F("ACK CMD Set to Saturation+2 END"));break;
  case 0x53:
    myCAM.OV5642_set_Color_Saturation(Saturation1);temp = 0xff;
   Serial.println(F("ACK CMD Set to Saturation+1 END"));break;
   case 0x54:
    myCAM.OV5642_set_Color_Saturation(Saturation0);temp = 0xff;
   Serial.println(F("ACK CMD Set to Saturation+0 END"));break;
   case 0x55:
    myCAM.OV5642_set_Color_Saturation(Saturation_1);temp = 0xff;
   Serial.println(F("ACK CMD Set to Saturation-1 END"));break;
   case 0x56:
    myCAM.OV5642_set_Color_Saturation(Saturation_2);temp = 0xff;
   Serial.println(F("ACK CMD Set to Saturation-2"));break;
    case 0x57:
    myCAM.OV5642_set_Color_Saturation(Saturation_3);temp = 0xff;
   Serial.println(F("ACK CMD Set to Saturation-3 END"));break;
   case 0x58:
  myCAM.OV5642_set_Light_Mode(Saturation_4);temp = 0xff;
   Serial.println(F("ACK CMD Set to Saturation-4 END"));break; 
   case 0x60:
  myCAM.OV5642_set_Brightness(Brightness4);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness+4 END"));break;
  case 0x61:
  myCAM.OV5642_set_Brightness(Brightness3);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness+3 END"));break; 
  case 0x62:
  myCAM.OV5642_set_Brightness(Brightness2);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness+2 END"));break; 
   case 0x63:
  myCAM.OV5642_set_Brightness(Brightness1);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness+1 END"));break; 
   case 0x64:
  myCAM.OV5642_set_Brightness(Brightness0);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness+0 END"));break; 
    case 0x65:
  myCAM.OV5642_set_Brightness(Brightness_1);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness-1 END"));break; 
     case 0x66:
  myCAM.OV5642_set_Brightness(Brightness_2);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness-2 END"));break; 
    case 0x67:
  myCAM.OV5642_set_Brightness(Brightness_3);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness-3 END"));break; 
    case 0x68:
  myCAM.OV5642_set_Brightness(Brightness_4);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness-4 END"));break;
case 0x70:
  myCAM.OV5642_set_Contrast(Contrast4);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast+4 END"));break;
  case 0x71:
  myCAM.OV5642_set_Contrast(Contrast3);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast+3 END"));break; 
  case 0x72:
  myCAM.OV5642_set_Contrast(Contrast2);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast+2 END"));break; 
   case 0x73:
  myCAM.OV5642_set_Contrast(Contrast1);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast+1 END"));break; 
   case 0x74:
  myCAM.OV5642_set_Contrast(Contrast0);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast+0 END"));break; 
    case 0x75:
  myCAM.OV5642_set_Contrast(Contrast_1);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast-1 END"));break; 
     case 0x76:
  myCAM.OV5642_set_Contrast(Contrast_2);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast-2 END"));break; 
    case 0x77:
  myCAM.OV5642_set_Contrast(Contrast_3);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast-3 END"));break; 
    case 0x78:
  myCAM.OV5642_set_Contrast(Contrast_4);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast-4 END"));break;
   case 0x80: 
    myCAM.OV5642_set_hue(degree_180);temp = 0xff;
     Serial.println(F("ACK CMD Set to -180 degree END"));break;   
   case 0x81: 
   myCAM.OV5642_set_hue(degree_150);temp = 0xff;
     Serial.println(F("ACK CMD Set to -150 degree END"));break;  
   case 0x82: 
   myCAM.OV5642_set_hue(degree_120);temp = 0xff;
     Serial.println(F("ACK CMD Set to -120 degree END"));break;  
   case 0x83: 
   myCAM.OV5642_set_hue(degree_90);temp = 0xff;
     Serial.println(F("ACK CMD Set to -90 degree END"));break;   
    case 0x84: 
   myCAM.OV5642_set_hue(degree_60);temp = 0xff;
     Serial.println(F("ACK CMD Set to -60 degree END"));break;   
    case 0x85: 
   myCAM.OV5642_set_hue(degree_30);temp = 0xff;
     Serial.println(F("ACK CMD Set to -30 degree END"));break;  
     case 0x86: 
   myCAM.OV5642_set_hue(degree_0);temp = 0xff;
     Serial.println(F("ACK CMD Set to 0 degree END"));break; 
   case 0x87: 
   myCAM.OV5642_set_hue(degree30);temp = 0xff;
     Serial.println(F("ACK CMD Set to 30 degree END"));break;
   case 0x88: 
   myCAM.OV5642_set_hue(degree60);temp = 0xff;
     Serial.println(F("ACK CMD Set to 60 degree END"));break;
    case 0x89: 
   myCAM.OV5642_set_hue(degree90);temp = 0xff;
     Serial.println(F("ACK CMD Set to 90 degree END"));break;
     case 0x8a: 
   myCAM.OV5642_set_hue(degree120);temp = 0xff;
     Serial.println(F("ACK CMD Set to 120 degree END"));break ; 
   case 0x8b: 
   myCAM.OV5642_set_hue(degree150);temp = 0xff;
     Serial.println(F("ACK CMD Set to 150 degree END"));break ;
   case 0x90: 
   myCAM.OV5642_set_Special_effects(Normal);temp = 0xff;
     Serial.println(F("ACK CMD Set to Normal END"));break ;
      case 0x91: 
   myCAM.OV5642_set_Special_effects(BW);temp = 0xff;
     Serial.println(F("ACK CMD Set to BW END"));break ;
    case 0x92: 
   myCAM.OV5642_set_Special_effects(Bluish);temp = 0xff;
     Serial.println(F("ACK CMD Set to Bluish END"));break ;
      case 0x93: 
   myCAM.OV5642_set_Special_effects(Sepia);temp = 0xff;
     Serial.println(F("ACK CMD Set to Sepia END"));break ;
    case 0x94: 
   myCAM.OV5642_set_Special_effects(Reddish);temp = 0xff;
     Serial.println(F("ACK CMD Set to Reddish END"));break ;
   case 0x95: 
   myCAM.OV5642_set_Special_effects(Greenish);temp = 0xff;
     Serial.println(F("ACK CMD Set to Greenish END"));break ;
   case 0x96: 
   myCAM.OV5642_set_Special_effects(Negative);temp = 0xff;
     Serial.println(F("ACK CMD Set to Negative END"));break ;
   case 0xA0: 
   myCAM.OV5642_set_Exposure_level(Exposure_17_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to -1.7EV"));break ;  
     case 0xA1: 
   myCAM.OV5642_set_Exposure_level(Exposure_13_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to -1.3EV END"));break ;
      case 0xA2: 
   myCAM.OV5642_set_Exposure_level(Exposure_10_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to -1.0EV END"));break ; 
    case 0xA3: 
   myCAM.OV5642_set_Exposure_level(Exposure_07_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to -0.7EV END"));break ;
     case 0xA4: 
   myCAM.OV5642_set_Exposure_level(Exposure_03_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to -0.3EV END"));break ;
   case 0xA5: 
   myCAM.OV5642_set_Exposure_level(Exposure_default);temp = 0xff;
     Serial.println(F("ACK CMD Set to -Exposure_default END"));break ;
    case 0xA6: 
   myCAM.OV5642_set_Exposure_level(Exposure07_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to 0.7EV END"));break ;  
   case 0xA7: 
   myCAM.OV5642_set_Exposure_level(Exposure10_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to 1.0EV END"));break ;
    case 0xA8: 
   myCAM.OV5642_set_Exposure_level(Exposure13_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to 1.3EV END"));break ; 
    case 0xA9: 
   myCAM.OV5642_set_Exposure_level(Exposure17_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to 1.7EV END"));break ; 
   case 0xB0: 
   myCAM.OV5642_set_Sharpness(Auto_Sharpness_default);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Sharpness default END"));break ; 
    case 0xB1: 
   myCAM.OV5642_set_Sharpness(Auto_Sharpness1);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Sharpness +1 END"));break ; 
    case 0xB2: 
   myCAM.OV5642_set_Sharpness(Auto_Sharpness2);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Sharpness +2 END"));break ; 
      case 0xB3: 
   myCAM.OV5642_set_Sharpness(Manual_Sharpnessoff);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Manual Sharpness off END"));break ; 
     case 0xB4: 
     myCAM.OV5642_set_Sharpness(Manual_Sharpness1);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Manual Sharpness +1 END"));break ;
     case 0xB5: 
     myCAM.OV5642_set_Sharpness(Manual_Sharpness2);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Manual Sharpness +2 END"));break ; 
     case 0xB6: 
     myCAM.OV5642_set_Sharpness(Manual_Sharpness3);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Manual Sharpness +3 END"));break ;
     case 0xB7: 
     myCAM.OV5642_set_Sharpness(Manual_Sharpness4);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Manual Sharpness +4 END"));break ;
    case 0xB8: 
     myCAM.OV5642_set_Sharpness(Manual_Sharpness5);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Manual Sharpness +5 END"));break ;  
    case 0xC0: 
     myCAM.OV5642_set_Mirror_Flip(MIRROR);temp = 0xff;
     Serial.println(F("ACK CMD Set to MIRROR END"));break ;  
    case 0xC1: 
     myCAM.OV5642_set_Mirror_Flip(FLIP);temp = 0xff;
     Serial.println(F("ACK CMD Set to FLIP END"));break ; 
    case 0xC2: 
     myCAM.OV5642_set_Mirror_Flip(MIRROR_FLIP);temp = 0xff;
     Serial.println(F("ACK CMD Set to MIRROR&FLIP END"));break ;
    case 0xC3: 
     myCAM.OV5642_set_Mirror_Flip(Normal);temp = 0xff;
     Serial.println(F("ACK CMD Set to Normal END"));break ;
     case 0xD0: 
     myCAM.OV5642_set_Compress_quality(high_quality);temp = 0xff;
     Serial.println(F("ACK CMD Set to high quality END"));break ;
      case 0xD1: 
     myCAM.OV5642_set_Compress_quality(default_quality);temp = 0xff;
     Serial.println(F("ACK CMD Set to default quality END"));break ;
      case 0xD2: 
     myCAM.OV5642_set_Compress_quality(low_quality);temp = 0xff;
     Serial.println(F("ACK CMD Set to low quality END"));break ;

      case 0xE0: 
     myCAM.OV5642_Test_Pattern(Color_bar);temp = 0xff;
     Serial.println(F("ACK CMD Set to Color bar END"));break ;
      case 0xE1: 
     myCAM.OV5642_Test_Pattern(Color_square);temp = 0xff;
     Serial.println(F("ACK CMD Set to Color square END"));break ;
      case 0xE2: 
     myCAM.OV5642_Test_Pattern(BW_square);temp = 0xff;
     Serial.println(F("ACK CMD Set to BW square END"));break ;
     case 0xE3: 
     myCAM.OV5642_Test_Pattern(DLI);temp = 0xff;
     Serial.println(F("ACK CMD Set to DLI END"));break ;
      default:
      break;
  }
}
if (mode == 1)
{
  if (start_capture == 1)
  {
    myCAM.flush_fifo();
    myCAM.clear_fifo_flag();
    myCAM.start_capture();
    start_capture = 0;
  }

  if (myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
      uint32_t length = myCAM.read_fifo_length();
      

      if (prev_valid) {
          int32_t len_diff = abs((int32_t)length - (int32_t)prev_len);
          if (len_diff < LEN_TOL) { 
              Serial.write(0x53);
              myCAM.clear_fifo_flag();
              return;
          }
      }

      read_fifo_burst(myCAM);

      prev_len = length;
      prev_valid = true;
      myCAM.clear_fifo_flag();
  }
}

else if (mode == 2)
{
  while (1)
  {
    temp = Serial.read();
    if (temp == 0x21)
    {
      start_capture = 0;
      mode = 0;
      Serial.println(F("ACK CMD CAM stop video streaming. END"));
      break;
    }
    switch(temp){
       case 0x40:
    myCAM.OV5642_set_Light_Mode(Advanced_AWB);temp = 0xff;
     Serial.println(F("ACK CMD Set to Advanced_AWB END"));break;
    case 0x41:
    myCAM.OV5642_set_Light_Mode(Simple_AWB);temp = 0xff;
     Serial.println(F("ACK CMD Set to Simple_AWB END"));break;
     case 0x42:
    myCAM.OV5642_set_Light_Mode(Manual_day);temp = 0xff;
     Serial.println(F("ACK CMD Set to Manual_day END"));break;
     case 0x43:
    myCAM.OV5642_set_Light_Mode(Manual_A);temp = 0xff;
     Serial.println(F("ACK CMD Set to Manual_A END"));break;
     case 0x44:
    myCAM.OV5642_set_Light_Mode(Manual_cwf);temp = 0xff;
     Serial.println(F("ACK CMD Set to Manual_cwf END"));break;
     case 0x45:
    myCAM.OV5642_set_Light_Mode(Manual_cloudy);temp = 0xff;
     Serial.println(F("ACK CMD Set to Manual_cloudy END"));break;
      case 0x50:
    myCAM.OV5642_set_Color_Saturation(Saturation4);temp = 0xff;
     Serial.println(F("ACK CMD Set to Saturation+4 END"));break;
   case 0x51:
      myCAM.OV5642_set_Color_Saturation(Saturation3);temp = 0xff;
     Serial.println(F("ACK CMD Set to Saturation+3 END"));break;
   case 0x52:
    myCAM.OV5642_set_Color_Saturation(Saturation2);temp = 0xff;
   Serial.println(F("ACK CMD Set to Saturation+2 END"));break;
  case 0x53:
    myCAM.OV5642_set_Color_Saturation(Saturation1);temp = 0xff;
   Serial.println(F("ACK CMD Set to Saturation+1 END"));break;
   case 0x54:
    myCAM.OV5642_set_Color_Saturation(Saturation0);temp = 0xff;
   Serial.println(F("ACK CMD Set to Saturation+0 END"));break;
   case 0x55:
    myCAM.OV5642_set_Color_Saturation(Saturation_1);temp = 0xff;
   Serial.println(F("ACK CMD Set to Saturation-1 END"));break;
   case 0x56:
    myCAM.OV5642_set_Color_Saturation(Saturation_2);temp = 0xff;
   Serial.println(F("ACK CMD Set to Saturation-2 END"));break;
    case 0x57:
    myCAM.OV5642_set_Color_Saturation(Saturation_3);temp = 0xff;
   Serial.println(F("ACK CMD Set to Saturation-3 END"));break;
   case 0x58:
  myCAM.OV5642_set_Light_Mode(Saturation_4);temp = 0xff;
   Serial.println(F("ACK CMD Set to Saturation-4 END"));break; 
   case 0x60:
  myCAM.OV5642_set_Brightness(Brightness4);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness+4 END"));break;
  case 0x61:
  myCAM.OV5642_set_Brightness(Brightness3);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness+3 END"));break; 
  case 0x62:
  myCAM.OV5642_set_Brightness(Brightness2);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness+2 END"));break; 
   case 0x63:
  myCAM.OV5642_set_Brightness(Brightness1);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness+1 END"));break; 
   case 0x64:
  myCAM.OV5642_set_Brightness(Brightness0);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness+0 END"));break; 
    case 0x65:
  myCAM.OV5642_set_Brightness(Brightness_1);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness-1 END"));break; 
     case 0x66:
  myCAM.OV5642_set_Brightness(Brightness_2);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness-2 END"));break; 
    case 0x67:
  myCAM.OV5642_set_Brightness(Brightness_3);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness-3 END"));break; 
    case 0x68:
  myCAM.OV5642_set_Brightness(Brightness_4);temp = 0xff;
   Serial.println(F("ACK CMD Set to Brightness-4 END"));break;
case 0x70:
  myCAM.OV5642_set_Contrast(Contrast4);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast+4 END"));break;
  case 0x71:
  myCAM.OV5642_set_Contrast(Contrast3);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast+3 END"));break; 
  case 0x72:
  myCAM.OV5642_set_Contrast(Contrast2);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast+2 END"));break; 
   case 0x73:
  myCAM.OV5642_set_Contrast(Contrast1);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast+1 END"));break; 
   case 0x74:
  myCAM.OV5642_set_Contrast(Contrast0);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast+0 END"));break; 
    case 0x75:
  myCAM.OV5642_set_Contrast(Contrast_1);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast-1 END"));break; 
     case 0x76:
  myCAM.OV5642_set_Contrast(Contrast_2);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast-2 END"));break; 
    case 0x77:
  myCAM.OV5642_set_Contrast(Contrast_3);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast-3 END"));break; 
    case 0x78:
  myCAM.OV5642_set_Contrast(Contrast_4);temp = 0xff;
   Serial.println(F("ACK CMD Set to Contrast-4 END"));break;
   case 0x80: 
    myCAM.OV5642_set_hue(degree_180);temp = 0xff;
     Serial.println(F("ACK CMD Set to -180 degree END"));break;   
   case 0x81: 
   myCAM.OV5642_set_hue(degree_150);temp = 0xff;
     Serial.println(F("ACK CMD Set to -150 degree END"));break;  
   case 0x82: 
   myCAM.OV5642_set_hue(degree_120);temp = 0xff;
     Serial.println(F("ACK CMD Set to -120 degree END"));break;  
   case 0x83: 
   myCAM.OV5642_set_hue(degree_90);temp = 0xff;
     Serial.println(F("ACK CMD Set to -90 degree END"));break;   
    case 0x84: 
   myCAM.OV5642_set_hue(degree_60);temp = 0xff;
     Serial.println(F("ACK CMD Set to -60 degree END"));break;   
    case 0x85: 
   myCAM.OV5642_set_hue(degree_30);temp = 0xff;
     Serial.println(F("ACK CMD Set to -30 degree END"));break;  
     case 0x86: 
   myCAM.OV5642_set_hue(degree_0);temp = 0xff;
     Serial.println(F("ACK CMD Set to 0 degree END"));break; 
   case 0x87: 
   myCAM.OV5642_set_hue(degree30);temp = 0xff;
     Serial.println(F("ACK CMD Set to 30 degree END"));break;
   case 0x88: 
   myCAM.OV5642_set_hue(degree60);temp = 0xff;
     Serial.println(F("ACK CMD Set to 60 degree END"));break;
    case 0x89: 
   myCAM.OV5642_set_hue(degree90);temp = 0xff;
     Serial.println(F("ACK CMD Set to 90 degree END"));break;
     case 0x8a: 
   myCAM.OV5642_set_hue(degree120);temp = 0xff;
     Serial.println(F("ACK CMD Set to 120 degree END"));break ; 
   case 0x8b: 
   myCAM.OV5642_set_hue(degree150);temp = 0xff;
     Serial.println(F("ACK CMD Set to 150 degree END"));break ;
  case 0x90: 
   myCAM.OV5642_set_Special_effects(Normal);temp = 0xff;
     Serial.println(F("ACK CMD Set to Normal END"));break ;
      case 0x91: 
   myCAM.OV5642_set_Special_effects(BW);temp = 0xff;
     Serial.println(F("ACK CMD Set to BW END"));break ;
    case 0x92: 
   myCAM.OV5642_set_Special_effects(Bluish);temp = 0xff;
     Serial.println(F("ACK CMD Set to Bluish END"));break ;
      case 0x93: 
   myCAM.OV5642_set_Special_effects(Sepia);temp = 0xff;
     Serial.println(F("ACK CMD Set to Sepia END"));break ;
    case 0x94: 
   myCAM.OV5642_set_Special_effects(Reddish);temp = 0xff;
     Serial.println(F("ACK CMD Set to Reddish END"));break ;
   case 0x95: 
   myCAM.OV5642_set_Special_effects(Greenish);temp = 0xff;
     Serial.println(F("ACK CMD Set to Greenish END"));break ;
   case 0x96: 
   myCAM.OV5642_set_Special_effects(Negative);temp = 0xff;
     Serial.println(F("ACK CMD Set to Negative END"));break ;
   case 0xA0: 
   myCAM.OV5642_set_Exposure_level(Exposure_17_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to -1.7EV END"));break ;  
     case 0xA1: 
   myCAM.OV5642_set_Exposure_level(Exposure_13_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to -1.3EV END"));break ;
      case 0xA2: 
   myCAM.OV5642_set_Exposure_level(Exposure_10_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to -1.0EV END"));break ; 
    case 0xA3: 
   myCAM.OV5642_set_Exposure_level(Exposure_07_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to -0.7EV END"));break ;
     case 0xA4: 
   myCAM.OV5642_set_Exposure_level(Exposure_03_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to -0.3EV END"));break ;
   case 0xA5: 
   myCAM.OV5642_set_Exposure_level(Exposure_default);temp = 0xff;
     Serial.println(F("ACK CMD Set to -Exposure_default END"));break ;
    case 0xA6: 
   myCAM.OV5642_set_Exposure_level(Exposure07_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to 0.7EV END"));break ;  
   case 0xA7: 
   myCAM.OV5642_set_Exposure_level(Exposure10_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to 1.0EV END"));break ;
    case 0xA8: 
   myCAM.OV5642_set_Exposure_level(Exposure13_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to 1.3EV END"));break ; 
    case 0xA9: 
   myCAM.OV5642_set_Exposure_level(Exposure17_EV);temp = 0xff;
     Serial.println(F("ACK CMD Set to 1.7EV END"));break ; 
   case 0xB0: 
   myCAM.OV5642_set_Sharpness(Auto_Sharpness_default);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Sharpness default END"));break ; 
    case 0xB1: 
   myCAM.OV5642_set_Sharpness(Auto_Sharpness1);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Sharpness +1 END"));break ; 
    case 0xB2: 
   myCAM.OV5642_set_Sharpness(Auto_Sharpness2);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Sharpness +2 END"));break ; 
      case 0xB3: 
   myCAM.OV5642_set_Sharpness(Manual_Sharpnessoff);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Manual Sharpness off END"));break ; 
     case 0xB4: 
     myCAM.OV5642_set_Sharpness(Manual_Sharpness1);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Manual Sharpness +1 END"));break ;
     case 0xB5: 
     myCAM.OV5642_set_Sharpness(Manual_Sharpness2);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Manual Sharpness +2 END"));break ; 
     case 0xB6: 
     myCAM.OV5642_set_Sharpness(Manual_Sharpness3);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Manual Sharpness +3 END"));break ;
     case 0xB7: 
     myCAM.OV5642_set_Sharpness(Manual_Sharpness4);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Manual Sharpness +4 END"));break ;
    case 0xB8: 
     myCAM.OV5642_set_Sharpness(Manual_Sharpness5);temp = 0xff;
     Serial.println(F("ACK CMD Set to Auto Manual Sharpness +5 END"));break ;  
    case 0xC0: 
     myCAM.OV5642_set_Mirror_Flip(MIRROR);temp = 0xff;
     Serial.println(F("ACK CMD Set to MIRROR END"));break ;  
    case 0xC1: 
     myCAM.OV5642_set_Mirror_Flip(FLIP);temp = 0xff;
     Serial.println(F("ACK CMD Set to FLIP END"));break ; 
    case 0xC2: 
     myCAM.OV5642_set_Mirror_Flip(MIRROR_FLIP);temp = 0xff;
     Serial.println(F("ACK CMD Set to MIRROR&FLIP END"));break ;
    case 0xC3: 
     myCAM.OV5642_set_Mirror_Flip(Normal);temp = 0xff;
     Serial.println(F("ACK CMD Set to Normal END"));break ;
     case 0xD0: 
     myCAM.OV5642_set_Compress_quality(high_quality);temp = 0xff;
     Serial.println(F("ACK CMD Set to high quality END"));break ;
      case 0xD1: 
     myCAM.OV5642_set_Compress_quality(default_quality);temp = 0xff;
     Serial.println(F("ACK CMD Set to default quality END"));break ;
      case 0xD2: 
     myCAM.OV5642_set_Compress_quality(low_quality);temp = 0xff;
     Serial.println(F("ACK CMD Set to low quality END"));break ;

      case 0xE0: 
     myCAM.OV5642_Test_Pattern(Color_bar);temp = 0xff;
     Serial.println(F("ACK CMD Set to Color bar END"));break ;
      case 0xE1: 
     myCAM.OV5642_Test_Pattern(Color_square);temp = 0xff;
     Serial.println(F("ACK CMD Set to Color square END"));break ;
      case 0xE2: 
     myCAM.OV5642_Test_Pattern(BW_square);temp = 0xff;
     Serial.println(F("ACK CMD Set to BW square END"));break ;
     case 0xE3: 
     myCAM.OV5642_Test_Pattern(DLI);temp = 0xff;
     Serial.println(F("ACK CMD Set to DLI END"));break ;
      
      }
    if (start_capture == 2)
    {
      myCAM.flush_fifo();
      myCAM.clear_fifo_flag();
      myCAM.start_capture();
      start_capture = 0;
    }
    if (myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK))
    {
      uint32_t length = 0;
      length = myCAM.read_fifo_length();
      if ((length >= MAX_FIFO_SIZE) | (length == 0))
      {
        myCAM.clear_fifo_flag();
        start_capture = 2;
        continue;
      }
      myCAM.CS_LOW();
      myCAM.set_fifo_burst();
      temp =  SPI.transfer(0x00);
      length --;
      while ( length-- )
      {
        temp_last = temp;
        temp =  SPI.transfer(0x00);
        if (is_header == true)
        {
          Serial.write(temp);
        }
        else if ((temp == 0xD8) & (temp_last == 0xFF))
        {
          is_header = true;
          Serial.println(F("ACK IMG END"));
          Serial.write(temp_last);
          Serial.write(temp);
        }
        if ( (temp == 0xD9) && (temp_last == 0xFF) ) 
        break;
        delayMicroseconds(4);
      }
      myCAM.CS_HIGH();
      myCAM.clear_fifo_flag();
      start_capture = 2;
      is_header = false;
    }
  }
}
else if (mode == 3)
{
  if (start_capture == 3)
  {
    myCAM.flush_fifo();
    myCAM.clear_fifo_flag();
    myCAM.start_capture();
    start_capture = 0;
  }
  if (myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK))
  {
    Serial.println(F("ACK CMD CAM Capture Done. END"));delay(50);
    uint8_t temp, temp_last;
    uint32_t length = 0;
    length = myCAM.read_fifo_length();
    if (length >= MAX_FIFO_SIZE ) 
    {
      Serial.println(F("ACK CMD Over size. END"));
      myCAM.clear_fifo_flag();
      return;
    }
    if (length == 0 ) 
    {
      Serial.println(F("ACK CMD Size is 0. END"));
      myCAM.clear_fifo_flag();
      return;
    }
    myCAM.CS_LOW();
    myCAM.set_fifo_burst();
    
    Serial.write(0xFF);
    Serial.write(0xAA);
    for (temp = 0; temp < BMPIMAGEOFFSET; temp++)
    {
      Serial.write(pgm_read_byte(&bmp_header[temp]));
    }
    char VH, VL;
    int i = 0, j = 0;
    for (i = 0; i < 240; i++)
    {
      for (j = 0; j < 320; j++)
      {
        VH = SPI.transfer(0x00);;
        VL = SPI.transfer(0x00);;
        Serial.write(VL);
        delayMicroseconds(12);
        Serial.write(VH);
        delayMicroseconds(12);
      }
    }
    Serial.write(0xBB);
    Serial.write(0xCC);
    myCAM.CS_HIGH();
    myCAM.clear_fifo_flag();
  }
}
}
uint8_t read_fifo_burst(ArduCAM myCAM)
{
  uint8_t temp = 0, temp_last = 0;
  uint32_t length = 0;
  length = myCAM.read_fifo_length();
  if (length >= MAX_FIFO_SIZE) 
  {
    Serial.println(F("ACK CMD Over size. END"));
    return 0;
  }
  if (length == 0 ) 
  {
    Serial.println(F("ACK CMD Size is 0. END"));
    return 0;
  }
  myCAM.CS_LOW();
  myCAM.set_fifo_burst();
  length --;
  while ( length-- )
  {
    temp_last = temp;
    temp =  SPI.transfer(0x00);
    if (is_header == true)
    {
      Serial.write(temp);
    }
    else if ((temp == 0xD8) & (temp_last == 0xFF))
    {
      is_header = true;
      Serial.println(F("ACK IMG END"));
      Serial.write(temp_last);
      Serial.write(temp);
    }
    if ( (temp == 0xD9) && (temp_last == 0xFF) ) //If find the end ,break while,
    break;
    delayMicroseconds(15);
  }
  myCAM.CS_HIGH();
  is_header = false;
  return 1;
}

static bool fifo_get_samples(ArduCAM &cam, uint32_t length, uint8_t *out)
{
  if (length == 0) return false;

  uint32_t step = length / SAMPLE_N;
  if (step == 0) step = 1;

  cam.CS_LOW();
  cam.set_fifo_burst();
  (void)SPI.transfer(0x00); 

  uint32_t idx = 0;
  uint32_t pos = 0;
  uint8_t b = 0;

  while (idx < SAMPLE_N && pos < length)
  {
    for (uint32_t k = 0; k < step - 1 && pos < length; k++, pos++)
      (void)SPI.transfer(0x00);

    if (pos < length)
    {
      b = SPI.transfer(0x00);
      out[idx++] = b;
      pos++;
    }
  }

  cam.CS_HIGH();

  while (idx < SAMPLE_N) out[idx++] = 0;

  return true;
}

static bool is_almost_same(const uint8_t *a, const uint8_t *b)
{
  uint32_t diff = 0;
  for (uint32_t i = 0; i < SAMPLE_N; i++)
    if (a[i] != b[i]) diff++;

  uint32_t diff_pct = (diff * 100) / SAMPLE_N;
  return (diff_pct <= SAME_RATIO_PCT);
}


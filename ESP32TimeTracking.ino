#include <TFT_eSPI.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <string>

WebServer server(80);

#define TWO_SEK 2000

/* Thread variables */
SemaphoreHandle_t mutex_state;
SemaphoreHandle_t mutex_time;

/* Display */

TFT_eSPI tft = TFT_eSPI(135, 240); // Invoke custom library

String first_lcd_line  =  "     NXP Cup    ";
String second_lcd_line =  "   Timed Race   ";

/* Buttons */
#define RESET_BUTTON 35

/* Light barrier */
#define LIGHT_BARRIER_SENSOR 2
bool light_barrier_positive_edge = false;
int last_light_barrier_state = HIGH;

/* State machine variables */
#define IDLE_STATE      0
#define READY_STATE     1
#define NOT_READY_STATE 2
#define RACE_STARTED    3
#define RUNNING         4
#define FINISHED_STATE  5
uint8_t state = IDLE_STATE;

/* Time Keeping variables */
unsigned long start_time    = 0;
unsigned long race_time     = 0;
unsigned long seconds       = 0;
unsigned long milliseconds  = 0;

void SetCursor(int x, int y, int font = 2)
{
    tft.setCursor(x*10, 10 + y*tft.fontHeight(), font);  
}

bool Connected = false;

std::string status = "";

void PrintIP()
{
    xSemaphoreTake(mutex_state, portMAX_DELAY);
    IPAddress ip = WiFi.localIP();
    SetCursor(3,2);
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    if(Connected)
      tft.print(ip);
    else
      tft.print("   AP MODE    ");
    xSemaphoreGive(mutex_state);  
}

void PrintTime(const char* name, int seconds, int milliseconds)
{
    // Writing the time to the LCD display
    SetCursor(3, 1);
    char buffer_sec[32];

    snprintf(buffer_sec, 32, "%03d.%03d", seconds, milliseconds);

    tft.print(buffer_sec);
    tft.print(" sec ");

    status = name;
    status +=":";
    status += buffer_sec;
    
    Serial.println(status.c_str());    
}

/* Display Thread Function */
void thread_lcd(void * parameter) {
  while (true) {
    xSemaphoreTake(mutex_state, portMAX_DELAY);
    // Setting variables depending on state
    switch (state) {
      case IDLE_STATE:
        first_lcd_line  = "     NXP Cup    ";
        second_lcd_line = "   Timed Race   ";
        
        tft.setTextColor(TFT_WHITE, TFT_BLACK);

        // if in IDLE_STATE print the second constant line
        SetCursor(0, 1);
        tft.println(second_lcd_line);

        Serial.println("State Idle");
        status = "IDLE";
        
        break;
      
      case READY_STATE:
        first_lcd_line  = "     NXP Cup    ";
        second_lcd_line = "      READY     ";
        
        tft.setTextColor(TFT_GREEN, TFT_BLACK);

        // if in IDLE_STATE print the second constant line
        SetCursor(0, 1);
        
        tft.println(second_lcd_line);
        
        Serial.println("State Ready");

        status = "Ready";       
        
        break;
     
      case NOT_READY_STATE:
        first_lcd_line  = "     NXP Cup    ";
        second_lcd_line = "    NOT READY   ";
        
        tft.setTextColor(TFT_RED, TFT_BLACK);

        // if in IDLE_STATE print the second constant line
        SetCursor(0, 1);
        tft.println(second_lcd_line);
        
        Serial.println("State Not Ready");

        status = "Not Ready";       
        
        break;
      
      case RACE_STARTED:
        first_lcd_line  = "      GO!       ";
        
        tft.setTextColor(TFT_WHITE, TFT_BLACK);

        // Converting time to format ss:mmm
        xSemaphoreTake(mutex_time, portMAX_DELAY);
        seconds = race_time / 1000;
        milliseconds = race_time % 1000;
        xSemaphoreGive(mutex_time);

        PrintTime("State Race", seconds, milliseconds);
        
        break;
      
      case RUNNING:
        first_lcd_line  = "      TIME      ";
        
        tft.setTextColor(TFT_WHITE, TFT_BLACK);

        // Converting time to format ss:mmm
        xSemaphoreTake(mutex_time, portMAX_DELAY);
        seconds = race_time / 1000;
        milliseconds = race_time % 1000;
        xSemaphoreGive(mutex_time);
        
        PrintTime("State Running", seconds, milliseconds);

        break;
      
      case FINISHED_STATE:
        first_lcd_line  = "   FINISHED!    ";
        
        tft.setTextColor(TFT_WHITE, TFT_BLACK);        

        // Converting time to format ss:mmm
        xSemaphoreTake(mutex_time, portMAX_DELAY);
        seconds = race_time / 1000;
        milliseconds = race_time % 1000;
        xSemaphoreGive(mutex_time);       
        PrintTime("State Finished", seconds, milliseconds);
       
        break;
    }
    xSemaphoreGive(mutex_state);

    SetCursor(0, 0);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print(first_lcd_line);
    PrintIP();
    yield();
  }

}

bool ResetNeeded = false;

void thread_measure(void * parameter) {
  while(1)
  {
    // reading light barrier input and positive edge detection
    int light_barrier_state = digitalRead(LIGHT_BARRIER_SENSOR);
    if((light_barrier_state != last_light_barrier_state) and light_barrier_state == LOW){
      light_barrier_positive_edge = true;
    }else{
      light_barrier_positive_edge = false;
    }
    last_light_barrier_state = light_barrier_state;
  
    // State machine
    xSemaphoreTake(mutex_state, portMAX_DELAY);
    // Reset
    if ((digitalRead(RESET_BUTTON) == LOW) || ResetNeeded) {
      ResetNeeded = false;
      state = IDLE_STATE;
      xSemaphoreTake(mutex_time, portMAX_DELAY);
      race_time = 0;
      xSemaphoreGive(mutex_time);
    }
    // if in idle but light barrier is interrupted -> not ready
    if (state == IDLE_STATE and (light_barrier_state == LOW)) {
      state = NOT_READY_STATE;
    // if in idle and light barrier is not interrupted -> ready
    } else if (state == IDLE_STATE and (light_barrier_state == HIGH)) {
      state = READY_STATE;
    // if in not ready state and light barrier is not interrupted anymore -> ready
    } else if(state == NOT_READY_STATE and (light_barrier_state == HIGH)){
      state = READY_STATE;
    // if in ready state and light barrier interrupted -> race started
    } else if (state == READY_STATE and (light_barrier_positive_edge == true)) {
      state = RACE_STARTED;
      xSemaphoreTake(mutex_time, portMAX_DELAY);
      start_time = millis();
      xSemaphoreGive(mutex_time);
    // race started
    } else if (state == RACE_STARTED) {
      state = RUNNING;
    // while race is running and no positive edge at light barrier is recognized, show time taken
    } else if (state == RUNNING and (light_barrier_positive_edge == false)) {
      state = RUNNING;
      xSemaphoreTake(mutex_time, portMAX_DELAY);
      race_time = millis() - start_time;
      xSemaphoreGive(mutex_time);
    // while running and light barrier is interrupted with positive edge -> race ended 
    // and waiting 2 seconds for a delay --> car is safely driven out of the sensor
    } else if (state == RUNNING and (light_barrier_positive_edge == true) and ((millis()-start_time)>TWO_SEK)) {
      state = FINISHED_STATE;
      xSemaphoreTake(mutex_time, portMAX_DELAY);
      race_time = millis() - start_time;
      xSemaphoreGive(mutex_time);
    }
    xSemaphoreGive(mutex_state);
    yield();
  } 
}

WiFiManager wifiManager;

void handleStatus() {
  std::string resp = "<html><head><title>NXP Cup Lap timer</title></head>"
                     "<body>"
                     "<table width=300>"
                     "<tr><td colspan=2 align=center><b>NXP Cup timer</b></td></tr>"
                     "<tr><td colspan=2 align=center>";
  resp += status.c_str();
  resp += "</td></tr>"
          "<tr><td align=center><form action=\"/status\"><input type=\"submit\" value=\"Update\"></form></td><td align=center><form action=\"/reset\"><input type=\"submit\" value=\"Reset\"></form></td></tr>"
          "</table>"
          "</form>"
          "</body>"
          "</html>";
  server.send(200, "text/html", resp.c_str());
}


void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

/* Setup function */
void setup() {

  mutex_state = xSemaphoreCreateMutex();
  mutex_time = xSemaphoreCreateMutex();
  
  /* Display */
  // set up the LCD's number of columns and rows:
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  SetCursor(0, 0, 2);
  // Print a message to the LCD. 
  tft.println(first_lcd_line);
  tft.print(second_lcd_line);

  /* Buttons */
  pinMode(RESET_BUTTON, INPUT_PULLUP); // Reset Button
  //pinMode(SECOND_BUTTON, INPUT_PULLUP); // unused Button

  /* Light barrier */
  pinMode(LIGHT_BARRIER_SENSOR, INPUT);

  // Open serial communications and wait for port to open:
  Serial.begin(115200);
  
  /* Threads */
  xTaskCreate(
                    thread_lcd,          /* Task function. */
                    "TaskLCD",        /* String with name of task. */
                    10000,            /* Stack size in bytes. */
                    NULL,             /* Parameter passed as input of the task */
                    2,                /* Priority of the task. */
                    NULL);            /* Task handle. */

  /* Threads */
  xTaskCreate(
                    thread_measure,          /* Task function. */
                    "TaskMeasure",        /* String with name of task. */
                    10000,            /* Stack size in bytes. */
                    NULL,             /* Parameter passed as input of the task */
                    3,                /* Priority of the task. */
                    NULL);            /* Task handle. */
  
  bool res = wifiManager.autoConnect("NXP_Timer", "87654321");
  if(res)
  {
    Connected = true;
    server.on("/", handleStatus);

    server.on("/status", handleStatus);

    server.on("/reset", []() {
      ResetNeeded = true;
      vTaskDelay(100);
      handleStatus();
    });

    server.begin();
    
    server.onNotFound(handleNotFound);
    
  }
}

/* Main loop */
void loop() {
 server.handleClient();
 vTaskDelay(1);
}

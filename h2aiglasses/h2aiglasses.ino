#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Base64.h>
#include "esp_camera.h"
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "";
const char* password = "";

// Google API key
const String apiKey = "";  // Replace with your Google API key
String PromptA = "";  // Prompt A
String PromptB = "";  // Prompt B
String currentPrompt = PromptA;  // Default prompt

// Firebase credentials
const String firebaseHost = "";  // Replace with your Firebase Realtime Database URL

// Web server
WebServer server(80);
String lastResult = "Waiting for image capture...";

// Pin definitions for XIAO ESP32S3 Sense
#define BUTTON_A_PIN D0       // Button A to capture image with Prompt A
#define BUTTON_B_PIN D1       // Button B to capture image with Prompt B
#define Flash_PIN D2          // External flash LED (optional)

// Camera pin definitions for XIAO ESP32S3 Sense
#define PWDN_GPIO_NUM -1    // Not used on XIAO ESP32S3 Sense
#define RESET_GPIO_NUM -1   // Not used on XIAO ESP32S3 Sense
#define XCLK_GPIO_NUM 10
#define SIOD_GPIO_NUM 40    // SDA
#define SIOC_GPIO_NUM 39    // SCL
#define Y9_GPIO_NUM 48
#define Y8_GPIO_NUM 11
#define Y7_GPIO_NUM 12
#define Y6_GPIO_NUM 14
#define Y5_GPIO_NUM 16
#define Y4_GPIO_NUM 18
#define Y3_GPIO_NUM 17
#define Y2_GPIO_NUM 15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM 47
#define PCLK_GPIO_NUM 13

// Replay button state
bool replayRequested = false;  // Track if replay is requested

// Function prototypes
void handleRoot();
void handleUpdatePrompt();
void captureAndAnalyzeImage();
void AnalyzeImage(const String& base64Image);
bool sendPostRequest(const String& payload, String& result);
void Flash();
void getResult();
void checkReplayButton();
void resetCamera();  // Function to reset the camera
void sendToFirebase(const String& path, const String& data);  // Function to send data to Firebase

// Camera configuration
camera_config_t config;

void setup() {
    Serial.begin(115200);

    // Connect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);  // Reduced delay
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("\nWiFi connected!");
    Serial.print("Web server started! Access it at: http://");
    Flash();
    Serial.println(WiFi.localIP());

    // Initialize buttons and Flash
    pinMode(BUTTON_A_PIN, INPUT_PULLUP);
    pinMode(BUTTON_B_PIN, INPUT_PULLUP);
    pinMode(Flash_PIN, OUTPUT);

    // Initialize web server
    server.on("/", handleRoot);
    server.on("/get_result", getResult);  // Endpoint to fetch the latest result
    server.on("/replay", checkReplayButton);  // Endpoint to handle replay button press
    server.on("/update_prompt", HTTP_POST, handleUpdatePrompt);  // Endpoint to update the prompt
    server.begin();

    // Initialize camera
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_XGA;  // Lower resolution
    config.jpeg_quality = 5;  // Lower quality
    config.fb_count = 2;

    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("Camera init failed");
    }
}

void loop() {
    server.handleClient();  // Handle client requests

    // Check if Button A is pressed
    if (digitalRead(BUTTON_A_PIN) == LOW) {
        delay(100);  // Reduce debounce delay
        Serial.println("Button A pressed! Using Prompt A...");
        currentPrompt = PromptA;  // Set prompt to Prompt A
        captureAndAnalyzeImage();  // Capture and analyze the image
    }

    // Check if Button B is pressed
    if (digitalRead(BUTTON_B_PIN) == LOW) {
        delay(100);  // Reduce debounce delay
        Serial.println("Button B pressed! Using Prompt B...");
        currentPrompt = PromptB;  // Set prompt to Prompt B
        captureAndAnalyzeImage();  // Capture and analyze the image
    }
}

// Handle root endpoint
void handleRoot() {
    String page = "<html><head><title>ESP32-CAM Analysis</title></head><body>";
    page += "<h1>ESP32-CAM AI Analysis</h1>";
    page += "<p id='message'>" + lastResult + "</p>";
    page += "<button onclick='speakText()'>🔊 Replay Message</button>";  // Replay button
    page += "<br><br>";
    page += "<form id='promptForm'>";
    page += "<label for='prompt'>Enter Prompt:</label><br>";
    page += "<input type='text' id='prompt' name='prompt' value='" + currentPrompt + "'><br><br>";
    page += "<input type='button' value='Update Prompt' onclick='updatePrompt()'>";
    page += "</form>";
    page += "<script>";
    page += "let speech = new SpeechSynthesisUtterance();";
    page += "speech.lang = 'en-US';";
    page += "let lastSpokenResult = '';";  // Track the last spoken result

    page += "function speakText() {";
    page += "  let msg = document.getElementById('message').innerText;";
    page += "  if (msg && msg !== 'Waiting for image capture...') {";
    page += "    speech.text = msg;";
    page += "    window.speechSynthesis.speak(speech);";
    page += "  }";
    page += "}";

    page += "function updateResult() {";
    page += "  fetch('/get_result').then(response => response.text()).then(data => {";
    page += "    if (data !== document.getElementById('message').innerText) {";
    page += "      document.getElementById('message').innerText = data;";
    page += "      if (data !== lastSpokenResult) {";  // Only speak if the result is new
    page += "        lastSpokenResult = data;";       // Update the last spoken result
    page += "        speakText();";                   // Speak the new result
    page += "      }";
    page += "    }";
    page += "  });";
    page += "}";

    page += "function checkReplay() {";
    page += "  fetch('/replay').then(response => response.text()).then(data => {";
    page += "    if (data.trim() === 'replay') {";
    page += "      speakText();";  // Replay the message
    page += "    }";
    page += "  });";
    page += "}";

    page += "function updatePrompt() {";
    page += "  let prompt = document.getElementById('prompt').value;";
    page += "  fetch('/update_prompt', {";
    page += "    method: 'POST',";
    page += "    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
    page += "    body: 'prompt=' + encodeURIComponent(prompt)";
    page += "  }).then(response => response.text()).then(data => {";
    page += "    alert('Prompt updated successfully!');";  // Notify the user
    page += "  });";
    page += "}";

    page += "setInterval(updateResult, 2000);";  // Fetch new result every 2 seconds
    page += "setInterval(checkReplay, 1000);";   // Check for replay button press every 1 second
    page += "</script>";
    page += "</body></html>";
    server.send(200, "text/html", page);
}

// Handle updating the prompt
void handleUpdatePrompt() {
    if (server.hasArg("prompt")) {
        currentPrompt = server.arg("prompt");
        Serial.println("Prompt updated to: " + currentPrompt);
        server.send(200, "text/plain", "Prompt updated");
    } else {
        server.send(400, "text/plain", "Bad Request");
    }
}

// Endpoint to fetch the latest result
void getResult() {
    server.send(200, "text/plain", lastResult);
}

// Handle replay button press
void checkReplayButton() {
    if (replayRequested) {
        server.send(200, "text/plain", "replay");
        replayRequested = false;  // Reset the flag
    } else {
        server.send(200, "text/plain", "idle");
    }
}

// Capture image and analyze it
void captureAndAnalyzeImage() {
    Serial.println("Capturing image...");
    Flash();

    // Reset the camera before capturing (optional, can be removed if stable)
    resetCamera();

    // Capture new image
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        lastResult = "Capture Failed";
        return;
    }

    // Debug: Print image buffer size
    Serial.printf("Image captured! Size: %d bytes\n", fb->len);

    // Encode image to Base64
    String base64Image = base64::encode(fb->buf, fb->len);

    // Debug: Print Base64-encoded image (first 100 characters)
    Serial.println("Base64 Image (first 100 chars): " + base64Image.substring(0, 100));

    // Release the buffer immediately
    esp_camera_fb_return(fb);

    if (base64Image.isEmpty()) {
        Serial.println("Failed to encode image");
        lastResult = "Encode Failed";
        return;
    }

    // Analyze the image
    AnalyzeImage(base64Image);
}

void AnalyzeImage(const String& base64Image) {
    Serial.println("Sending image for analysis...");

    // Create the JSON payload for Gemini
    DynamicJsonDocument doc(4096);
    JsonArray contents = doc.createNestedArray("contents");
    JsonObject content = contents.createNestedObject();
    JsonArray parts = content.createNestedArray("parts");

    // Add the text prompt
    JsonObject textPart = parts.createNestedObject();
    textPart["text"] = currentPrompt;

    // Add the image
    JsonObject imagePart = parts.createNestedObject();
    JsonObject inlineData = imagePart.createNestedObject("inline_data");
    inlineData["mime_type"] = "image/jpeg";  // Specify the MIME type
    inlineData["data"] = base64Image;        // Add the Base64-encoded image

    // Debug: Print the JSON payload
    String jsonPayload;
    serializeJson(doc, jsonPayload);
    Serial.println("JSON Payload: " + jsonPayload);

    String result;
    if (sendPostRequest(jsonPayload, result)) {
        Serial.println("Raw API Response: ");
        Serial.println(result);

        // Parse the Gemini response
        DynamicJsonDocument responseDoc(4096);
        DeserializationError error = deserializeJson(responseDoc, result);

        if (!error) {
            // Extract the response text
            lastResult = responseDoc["candidates"][0]["content"]["parts"][0]["text"].as<String>();
            Serial.println("Extracted Analysis:");
            Flash();
            if (currentPrompt == PromptA) {
                Serial.println("Prompt A Result:");
                sendToFirebase("/promptA.json", lastResult);  // Send result to Firebase under promptA.json
            } else if (currentPrompt == PromptB) {
                Serial.println("Prompt B Result:");
                sendToFirebase("/promptB.json", lastResult);  // Send result to Firebase under promptB.json
            }
            Serial.println(lastResult);
        } else {
            Serial.println("JSON Parsing Error!");
            lastResult = "Parsing Error";
        }
    } else {
        Serial.println("API Call Failed!");
        lastResult = "API Error";
    }
}

// Send POST request to Gemini API
bool sendPostRequest(const String& payload, String& result) {
    WiFiClientSecure client;
    client.setInsecure();  // Bypass SSL/TLS certificate verification (for testing only)

    if (!client.connect("generativelanguage.googleapis.com", 443)) {
        Serial.println("Connection to server failed!");
        return false;
    }

    HTTPClient http;
    http.begin(client, "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=" + apiKey);
    http.addHeader("Content-Type", "application/json");

    http.setTimeout(10000);  // Reduce timeout to 10 seconds

    Serial.println("Sending API Request...");
    Serial.println(payload);

    int retryCount = 0;
    int httpResponseCode = -1;

    while (retryCount < 1) {  // Reduce retries to 1
        httpResponseCode = http.POST(payload);
        Serial.print("HTTP Response Code: ");
        Serial.println(httpResponseCode);

        if (httpResponseCode > 0) {
            result = http.getString();
            Serial.println("API Response: ");
            Serial.println(result);
            http.end();
            return true;
        } else {
            Serial.println("HTTP request failed. Retrying...");
            retryCount++;
            delay(500);  // Reduce retry delay
        }
    }

    Serial.println("HTTP request failed after retries");
    result = "HTTP request failed";
    http.end();
    return false;
}

// Send data to Firebase
void sendToFirebase(const String& path, const String& data) {
    WiFiClientSecure client;
    client.setInsecure();  // Bypass SSL/TLS certificate verification (for testing only)

    HTTPClient http;
    http.begin(client, firebaseHost + path);
    http.addHeader("Content-Type", "application/json");

    // Create a JSON payload
    DynamicJsonDocument doc(4096);
    doc["result"] = data;
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    int httpResponseCode = http.PUT(jsonPayload);
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("Firebase Response: ");
        Serial.println(response);
    } else {
        Serial.println("Firebase PUT request failed");
    }

    http.end();
}

// Flash function for feedback
void Flash() {
    digitalWrite(Flash_PIN, HIGH);
    delay(50);  // Reduce flash duration
    digitalWrite(Flash_PIN, LOW);
}

// Reset camera function
void resetCamera() {
    esp_camera_deinit();  // Deinitialize the camera
    delay(50);  // Reduce delay
    esp_camera_init(&config);  // Reinitialize the camera
}

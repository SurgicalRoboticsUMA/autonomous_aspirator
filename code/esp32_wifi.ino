#define ROSSERIAL_ARDUINO_TCP
#include <ros.h>
#include <WiFi.h>
#include <std_msgs/String.h>
#include <std_msgs/Bool.h>

// --- CONFIGURACIÓN DE LA RED WIFI Y DEL NODO ROS ---

const char* ssid = "FX2000-986C";
const char* password = "af764128";


IPAddress server(192, 168, 1, 69);// IP nodo ROS PC mio69
const uint16_t serverPort = 11411;// Puerto donde rosserial_tcp escucha (NO debe ser 11311)

// IP estática para ESP32
IPAddress local_IP(192, 168, 1, 100);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8,8,8,8);
IPAddress secondaryDNS(8,8,4,4);

// --- OBJETOS ROS ---

ArduinoHardware hardware;
ros::NodeHandle nh;

std_msgs::String aspirator_msg;
std_msgs::String led_msg;

ros::Publisher aspirator("estado_aspirador", &aspirator_msg);
ros::Publisher led("estado_led", &led_msg);

// --- ESTADOS ---

bool estado_pin_cmd = false;   // comando manual directo al pin (topic aspirator/status)
bool estado_led = false;

bool posicion_ok = false;
bool sangre_ok = false;

// Estado real del aspirador
bool aspirador_activo = false;

// Histéresis temporal para apagar
unsigned long t_sangre_off_inicio = 0;
const unsigned long OFF_DELAY_MS = 500;

// Pines
const int PIN_aspirator = 19;
const int PIN_led = 18;

unsigned long last_state_pub = 0;
const unsigned long STATE_PUB_PERIOD_MS = 200;

// =====================================================
// PUBLICAR ESTADO REAL DEL ASPIRADOR
// =====================================================
void publishAspiratorState() {
  if (aspirador_activo) {
    aspirator_msg.data = "Encendido";
  } else {
    aspirator_msg.data = "Apagado";
  }
  aspirator.publish(&aspirator_msg);
}

// =====================================================
// CONTROL AUTOMÁTICO DEL ASPIRADOR
// =====================================================
void updateAspirator() {
  unsigned long now = millis();

  // -------------------------------------------------
  // 1) Si está APAGADO:
  //    solo se enciende cuando hay sangre y posición
  // -------------------------------------------------
  if (!aspirador_activo) {
    if (sangre_ok && posicion_ok) {
      aspirador_activo = true;
      t_sangre_off_inicio = 0;

      digitalWrite(PIN_aspirator, HIGH);
      Serial.println("Aspirador ENCENDIDO (sangre_ok && posicion_ok)");
      publishAspiratorState();
    } else {
      digitalWrite(PIN_aspirator, LOW);
    }
    return;
  }

  // -------------------------------------------------
  // 2) Si está ENCENDIDO:
  //    NO usamos posicion_ok para apagar
  //    solo sangre_ok con retardo temporal
  // -------------------------------------------------
  if (!sangre_ok) {
    if (t_sangre_off_inicio == 0) {
      t_sangre_off_inicio = now;
    }

    if (now - t_sangre_off_inicio >= OFF_DELAY_MS) {
      aspirador_activo = false;
      t_sangre_off_inicio = 0;

      digitalWrite(PIN_aspirator, LOW);
      Serial.println("Aspirador APAGADO (sangre_ok false mantenido)");
      publishAspiratorState();
    }
  } else {
    // Sigue habiendo sangre, mantenemos activo
    t_sangre_off_inicio = 0;
    digitalWrite(PIN_aspirator, HIGH);
  }
}

// =====================================================
// CALLBACK MANUAL DIRECTO DEL ASPIRADOR
// OJO: esto actúa como comando manual directo.
// Si lo usas, puede interferir con el modo automático.
// =====================================================
void estadoCallback(const std_msgs::Bool &msg) {
  estado_pin_cmd = msg.data;

  aspirador_activo = estado_pin_cmd;
  t_sangre_off_inicio = 0;

  digitalWrite(PIN_aspirator, aspirador_activo ? HIGH : LOW);

  if (aspirador_activo) {
    Serial.println("Aspirador ENCENDIDO (comando manual)");
  } else {
    Serial.println("Aspirador APAGADO (comando manual)");
  }

  publishAspiratorState();
}

// =====================================================
// CALLBACK LED
// =====================================================
void ledCallback(const std_msgs::Bool &msg) {
  estado_led = msg.data;
  digitalWrite(PIN_led, estado_led ? HIGH : LOW);

  if (estado_led) {
    led_msg.data = "LED encendido";
  } else {
    led_msg.data = "LED apagado";
  }

  led.publish(&led_msg);
}

// =====================================================
// CALLBACKS AUTOMÁTICOS
// =====================================================
void sangreOkCallback(const std_msgs::Bool &msg) {
  sangre_ok = msg.data;
  updateAspirator();
}

void posicionOkCallback(const std_msgs::Bool &msg) {
  posicion_ok = msg.data;
  updateAspirator();
}

// =====================================================
// SUSCRIPTORES
// =====================================================
ros::Subscriber<std_msgs::Bool> estado_sub("aspirator/status", estadoCallback);
ros::Subscriber<std_msgs::Bool> led_sub("aspirator/led", ledCallback);
ros::Subscriber<std_msgs::Bool> sangre_sub("/bleending/sangre_ok", sangreOkCallback);
ros::Subscriber<std_msgs::Bool> posicion_sub("posicion_ok", posicionOkCallback);

// =====================================================
// ESTADO WIFI
// =====================================================
void printWiFiStatus() {
  wl_status_t status = WiFi.status();
  Serial.print("Estado WiFi: ");
  switch (status) {
    case WL_NO_SHIELD: Serial.println("No hay hardware WiFi"); break;
    case WL_IDLE_STATUS: Serial.println("Idle, no está conectado"); break;
    case WL_NO_SSID_AVAIL: Serial.println("SSID no disponible"); break;
    case WL_SCAN_COMPLETED: Serial.println("Escaneo completado"); break;
    case WL_CONNECTED: Serial.println("Conectado"); break;
    case WL_CONNECT_FAILED: Serial.println("Fallo de conexión"); break;
    case WL_CONNECTION_LOST: Serial.println("Conexión perdida"); break;
    default: Serial.println("Estado desconocido"); break;
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_aspirator, OUTPUT);
  digitalWrite(PIN_aspirator, LOW);

  pinMode(PIN_led, OUTPUT);
  digitalWrite(PIN_led, LOW);

  Serial.println("Configurando IP estática...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(1000);

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Error al configurar IP estática");
  } else {
    Serial.println("IP estática configurada");
  }

  WiFi.begin(ssid, password);

  unsigned long startTime = millis();
  const unsigned long timeout = 20000;

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout) {
    delay(500);
    Serial.print(".");
    printWiFiStatus();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado");
    Serial.print("IP ESP32: ");
    Serial.println(WiFi.localIP());

    nh.getHardware()->setConnection(server, serverPort);
    nh.initNode();

    nh.advertise(aspirator);
    nh.advertise(led);

    nh.subscribe(estado_sub);
    nh.subscribe(led_sub);
    nh.subscribe(sangre_sub);
    nh.subscribe(posicion_sub);

    // Publicar estado inicial
    publishAspiratorState();

  } else {
    Serial.println("\nNo se pudo conectar a WiFi");
  }
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  if (!nh.connected()) {
    Serial.println("No conectado al nodo ROS");
  }

  nh.spinOnce();

  unsigned long now = millis();
  if (nh.connected() && (now - last_state_pub >= STATE_PUB_PERIOD_MS)) {
    publishAspiratorState();
    last_state_pub = now;
  }

  // IMPORTANTE:
  // 1000 ms era demasiado lento para esta aplicación.
  // 10 ms permite reaccionar bien sin saturar.
  delay(10);
}

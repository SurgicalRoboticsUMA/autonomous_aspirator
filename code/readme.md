# Sistema de detección de sangrado y control de aspiración

Este proyecto integra visión artificial, control robótico y hardware embebido para detectar una zona de sangrado, calcular un objetivo en 3D, mover un robot aspirador hacia ese punto y activar la aspiración de forma automática.

## Estructura del proyecto

El sistema está compuesto por tres bloques principales:

- **ESP32 + ROSSerial TCP**: controla el aspirador por wifi.
- **Nodo `potential_field_hybrid`**: mueve el robot aspirador hacia el objetivo detectado.
- **Nodo `bleending_detector_node`**: detecta sangre con una U-Net y publica el punto objetivo en 3D.

## Flujo general

1. La cámara RealSense captura imagen RGB y profundidad.
2. El nodo `bleending_detector_node` segmenta la sangre y calcula un punto objetivo 3D.
3. El nodo `potential_field_hybrid` transforma ese punto al frame del robot aspirador y genera una velocidad cartesiana.
4. El ESP32 recibe el estado de detección y posición, y enciende o apaga el aspirador.

---

## 1. Código ESP32

Este código conecta el ESP32 por WiFi con ROS mediante `rosserial_tcp` y controla dos salidas digitales:

- **Pin 19**: aspirador

### Funciones principales

- Conexión WiFi con IP estática.
- Comunicación con ROS por TCP.
- Control manual del aspirador mediante topic.
- Control automático del aspirador según detección de sangre y posición válida.
- Publicación del estado real del aspirador y del LED.

### Topics

**Suscriptores**
- `aspirator/status` (`std_msgs/Bool`): control manual del aspirador.
- `/bleending/sangre_ok` (`std_msgs/Bool`): indica si hay sangre suficiente para aspirar.
- `posicion_ok` (`std_msgs/Bool`): indica si la posición del robot es válida.

**Publicadores**
- `estado_aspirador` (`std_msgs/String`) 
- `estado_led` (`std_msgs/String`)

### Lógica de control

- El aspirador se enciende automáticamente cuando `sangre_ok` y `posicion_ok` son verdaderos.
- Una vez encendido, se mantiene activo mientras siga habiendo sangre.
- Se apaga solo si `sangre_ok` permanece en falso durante un pequeño retardo (`OFF_DELAY_MS`), evitando apagados bruscos.
- También puede activarse o apagarse manualmente desde ROS.

---

## 2. Nodo `potential_field_hybrid`

Este nodo implementa un controlador basado en **campos potenciales artificiales** para mover el robot aspirador hacia el punto de sangrado detectado.

### Funciones principales

- Recibe la pose del robot cámara y del robot aspirador.
- Recibe el punto objetivo detectado en el frame de la cámara.
- Transforma ese punto al frame base del robot aspirador.
- Calcula una velocidad cartesiana en dirección al objetivo.
- Publica dicha velocidad para el controlador del robot.

### Topics

**Suscriptores**
- `/auto/pose_topic` (`Float64MultiArray`): pose del robot cámara.
- `/teleop/pose_topic` (`Float64MultiArray`): pose del robot aspirador.
- `/bleending/punto_objetivo` (`geometry_msgs/Point`): objetivo 3D detectado.

**Publicadores**
- `/potential_field/cart_vel` (`geometry_msgs/Twist`): velocidad cartesiana.
- `/potential_field/target_in_base` (`geometry_msgs/Point`): objetivo transformado al frame del robot aspirador.

### Parámetros principales

- `k_att`: ganancia atractiva.
- `max_speed`: velocidad máxima.
- `dead_zone`: distancia mínima para dejar de mover el robot.
- `target_timeout`: tiempo máximo sin recibir un objetivo antes de detenerse.

---

## 3. Nodo `bleending_detector_node`

Este nodo realiza la detección visual de sangre usando una cámara **Intel RealSense** y una red neuronal **U-Net**.

### Funciones principales

- Captura imagen RGB y profundidad.
- Segmenta sangre mediante el modelo `unet_blood_segmentation_final.h5`.
- Limpia la máscara con operaciones morfológicas.
- Detecta blobs de sangre.
- Calcula varios centroides candidatos.
- Selecciona un objetivo 2D/3D (área más grande).
- Publica imágenes, centroides y el punto objetivo.
- Genera la señal `sangre_ok` para activar la aspiración.

### Topics

**Publicadores**
- `/bleending/centroids_2d` (`Float32MultiArray`): centroides calculados.
- `/bleending/sangre_centroides` (`geometry_msgs/Point`): centroides 3D.
- `/bleending/punto_objetivo` (`geometry_msgs/Point`): punto objetivo final.
- `/bleending/image_overlay` (`sensor_msgs/Image`): imagen con detecciones.
- `/bleending/image_raw_clean` (`sensor_msgs/Image`): imagen segmentada.
- `/bleending/sangre_ok` (`std_msgs/Bool`): activa/desactiva aspiración.

### Modos de selección del objetivo

- `C1`: centroide geométrico.
- `C2`: centroide del núcleo estable del blob.
- `C3`: centroide ponderado por novedad.
- `C4`: punto interno usando transformada de distancia.

### Lógica de aspiración visual

La señal `sangre_ok` se activa cuando el área detectada supera un umbral máximo y se desactiva cuando permanece por debajo de un umbral mínimo durante varios frames. Esto evita parpadeos en la activación.

---

## Dependencias

### ESP32
- `rosserial`
- `WiFi.h`

### ROS Python
- `rospy`
- `numpy`
- `opencv-python`
- `pyrealsense2`
- `tensorflow / keras`
- `cv_bridge`


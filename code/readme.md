# Bleeding Detection and Aspiration Control System

This project integrates computer vision, robotic control, and embedded hardware to detect a bleeding region, compute a 3D target, move a suction robot toward it, and automatically activate aspiration.

## Project Structure

The system is composed of three main modules:

- **ESP32 + ROSSerial TCP**: controls a relay that switches the aspirator.
- **Node `potential_field_hybrid`**: moves the suction robot toward the detected target.
- **Node `bleending_detector_node`**: detects blood using a U-Net model and publishes the 3D target.

## System Workflow

1. The RealSense camera captures RGB and depth data.
2. The `bleending_detector_node` segments blood and computes a 3D target point.
3. The `potential_field_hybrid` node transforms this point into the suction robot frame and generates a Cartesian velocity.
4. The ESP32 receives detection and position states and activates/deactivates a relay controlling the aspirator.

---

## 1. ESP32 Code - (esp32_wifi.ino)

This code connects the ESP32 to ROS via WiFi using `rosserial_tcp` and controls a digital output:

- **Pin 19**: relay controlling the aspirator

> Note: The ESP32 does not power the aspirator directly. It controls a relay that switches the aspirator on/off.

### Main Features

- WiFi connection with static IP.
- TCP communication with ROS.
- Manual control of the aspirator via topic.
- Automatic control based on blood detection and valid robot position.
- Publishes the real state of the aspirator and LED.

### Topics

**Subscribers**
- `aspirator/status` (`std_msgs/Bool`): manual control of the aspirator.
- `/bleending/sangre_ok` (`std_msgs/Bool`): indicates if blood is detected.
- `posicion_ok` (`std_msgs/Bool`): indicates if robot position is valid.

**Publishers**
- `estado_aspirador` (`std_msgs/String`) 
- `estado_led` (`std_msgs/String`)

### Control Logic

- The aspirator turns ON when both `sangre_ok` and `posicion_ok` are true.
- Once ON, it remains active while blood is still detected.
- It turns OFF only if `sangre_ok` stays false for a short delay (`OFF_DELAY_MS`), preventing flickering.
- It can also be manually controlled via ROS.

---

## 2. Node `potential_field_hybrid` - autonomous_aspirator_navigation.py

This node implements an **artificial potential field (APF)** controller to guide the suction robot toward the detected bleeding point.

### Main Features

- Receives pose of both camera robot and suction robot.
- Receives the detected 3D target point.
- Transforms the target into the suction robot base frame.
- Computes a Cartesian velocity toward the target.
- Publishes velocity commands.

### Topics

**Subscribers**
- `/auto/pose_topic` (`Float64MultiArray`): camera robot pose.
- `/teleop/pose_topic` (`Float64MultiArray`): suction robot pose.
- `/bleending/punto_objetivo` (`geometry_msgs/Point`): detected 3D target.

**Publishers**
- `/potential_field/cart_vel` (`geometry_msgs/Twist`): Cartesian velocity.
- `/potential_field/target_in_base` (`geometry_msgs/Point`): transformed target.

### Key Parameters

- `k_att`: attractive gain.
- `max_speed`: maximum velocity.
- `dead_zone`: distance threshold to stop movement.
- `target_timeout`: timeout for target validity.

---

## 3. Node `bleending_detector_node` - (bleeding_detection.py)

This node performs blood detection using an **Intel RealSense camera** and a **U-Net neural network**.

### Main Features

- Captures RGB and depth images.
- Segments blood using `unet_blood_segmentation_final.h5`.
- Cleans the mask using morphological operations.
- Detects blobs of blood.
- Computes multiple centroid candidates.
- Selects a 2D/3D target (largest blob).
- Publishes images, centroids, and the final target.
- Generates the `sangre_ok` signal.

### Topics

**Publishers**
- `/bleending/centroids_2d` (`Float32MultiArray`): computed centroids.
- `/bleending/sangre_centroides` (`geometry_msgs/Point`): 3D centroids.
- `/bleending/punto_objetivo` (`geometry_msgs/Point`): final target point.
- `/bleending/image_overlay` (`sensor_msgs/Image`): image with detections.
- `/bleending/image_raw_clean` (`sensor_msgs/Image`): segmented image.
- `/bleending/sangre_ok` (`std_msgs/Bool`): aspiration trigger signal.

### Target Selection Modes

- `C1`: geometric centroid.
- `C2`: stable core centroid.
- `C3`: novelty-weighted centroid.
- `C4`: internal point using distance transform.

### Aspiration Logic

The `sangre_ok` signal is activated when the detected area exceeds a maximum threshold and deactivated when it remains below a minimum threshold for several frames. This prevents flickering.

---

## Dependencies

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

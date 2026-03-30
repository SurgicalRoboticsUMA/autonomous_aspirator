#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import rospy
import numpy as np
from std_msgs.msg import Float64MultiArray
from geometry_msgs.msg import Point, Twist

class PotentialFieldHybrid:
    def __init__(self):
        rospy.init_node("potential_field_hybrid")

        # =============================================================
        # 1. CONFIGURACIÓN
        # =============================================================
        self.robot_pose_topic = "/auto/pose_topic"           # Robot cámara
        self.robot_pose_topic_teleop = "/teleop/pose_topic" # Robot aspirador

        # Parámetros APF
        self.k_att = 1.0
        self.max_speed = 0.05
        self.dead_zone = 0.005
        self.target_timeout = 0.5

        # =============================================================
        # 2. VARIABLES
        # =============================================================
        self.current_robot_pos = None
        self.target_in_base = None
        self.last_target_time = rospy.Time(0)

        self.T1_ = np.eye(4)
        self.T2_ = np.eye(4)

        # Transformación fija entre robots
        self.B_T_A = np.array([
            [-0.0134, -0.9999,  0.0098,  0.5458],
            [ 0.9997, -0.0136, -0.0211,  0.5305],
            [ 0.0212,  0.0095,  0.9997, -0.1375],
            [ 0.0,     0.0,     0.0,     1.0]
        ])

        # =============================================================
        # 3. SUSCRIPTORES
        # =============================================================
        self.sub_pose = rospy.Subscriber(self.robot_pose_topic, Float64MultiArray, self.cmd_pose1)

        self.sub_pose_teleop = rospy.Subscriber(self.robot_pose_topic_teleop,Float64MultiArray,self.cmd_pose2)

        self.sub_target = rospy.Subscriber("/bleending/punto_objetivo",Point,self.cb_target)

        # =============================================================
        # 4. PUBLICADORES
        # =============================================================
        self.pub_vel = rospy.Publisher("/potential_field/cart_vel",Twist,queue_size=10)

        # IMPORTANTE:
        # Publicamos el target ya transformado al frame del robot aspirador.
        # Este topic servirá para calcular tracking error correctamente.
        self.pub_target_base = rospy.Publisher("/potential_field/target_in_base",Point,queue_size=10)

        rospy.loginfo("Controlador listo. Esperando datos...")

    # =============================================================
    # CALLBACKS
    # =============================================================
    def cmd_pose1(self, msg):
        try:
            self.T1_ = np.array(msg.data).reshape(4, 4, order='F')
        except ValueError:
            rospy.logwarn_throttle(1.0, "Pose robot A mal formada.")

    def cmd_pose2(self, msg):
        try:
            self.T2_ = np.array(msg.data).reshape(4, 4, order='F')
            self.current_robot_pos = self.T2_[0:3, 3]
        except ValueError:
            rospy.logwarn_throttle(1.0, "Pose robot B mal formada.")

    def cb_target(self, msg):
        try:
            # Punto objetivo en frame cámara
            p_cam_vec = np.array([msg.x, msg.y, msg.z, 1.0])

            # Frame base robot A
            p_base_auto = self.T1_.dot(p_cam_vec)

            # Frame base robot B (robot aspirador)
            p_base_teleop = self.B_T_A.dot(p_base_auto)

            self.target_in_base = p_base_teleop[0:3]
            self.last_target_time = rospy.Time.now()

            # Publicar target ya transformado
            self.pub_target_base.publish(
                Point(
                    x=float(self.target_in_base[0]),
                    y=float(self.target_in_base[1]),
                    z=float(self.target_in_base[2])
                )
            )

        except Exception as e:
            rospy.logwarn_throttle(1.0, f"Error transformando target: {e}")

    # =============================================================
    # BUCLE DE CONTROL
    # =============================================================
    def control_loop(self):
        rate = rospy.Rate(50)

        while not rospy.is_shutdown():
            v_out = np.zeros(3)

            # Watchdog
            time_since_last_target = (rospy.Time.now() - self.last_target_time).to_sec()
            target_is_fresh = time_since_last_target < self.target_timeout

            if (
                self.current_robot_pos is not None and
                self.target_in_base is not None and
                target_is_fresh
            ):
                diff = self.target_in_base - self.current_robot_pos
                dist = np.linalg.norm(diff)

                if dist > self.dead_zone:
                    direction = diff / dist
                    speed = min(self.k_att * dist, self.max_speed)
                    v_out = direction * speed

            else:
                rospy.logwarn_throttle(
                    1.0,
                    f"STOP DEBUG | current_robot_pos is None: {self.current_robot_pos is None} | "
                    f"target_in_base is None: {self.target_in_base is None} | "
                    f"target_is_fresh: {target_is_fresh}"
                )

            msg = Twist()
            msg.linear.x = float(v_out[0])
            msg.linear.y = float(v_out[1])
            msg.linear.z = float(v_out[2])

            self.pub_vel.publish(msg)
            rate.sleep()


if __name__ == "__main__":
    try:
        PotentialFieldHybrid().control_loop()
    except rospy.ROSInterruptException:
        pass
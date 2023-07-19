<script lang="ts">
import { onMount } from 'svelte';
import {
    WebGLRenderer,
    PerspectiveCamera,
    Scene,
    Mesh,
    PlaneGeometry,
    ShadowMaterial,
    DirectionalLight,
    PCFSoftShadowMap,
    sRGBEncoding,
    AmbientLight,
    MathUtils,
    LoaderUtils
} from 'three';
import { XacroLoader } from 'xacro-parser';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import URDFLoader from 'urdf-loader';
import { servoBuffer } from '../../lib/socket'
import { lerp } from '../../lib/utils';
import uzip from 'uzip';
import { outControllerData } from '../../lib/store';

import {Matrix} from 'mathjs'
import { mat3, mat4, vec3, vec4 } from 'gl-matrix'
	import { debug } from 'svelte/internal';
	import type { Link } from 'svelte-routing';

let el: HTMLCanvasElement;
let scene, camera, renderer, robot, controls;
let angles = new Int8Array(12)

const servoNames = [
    "front_left_shoulder", "front_left_leg", "front_left_foot", 
    "front_right_shoulder", "front_right_leg", "front_right_foot", 
    "rear_left_shoulder", "rear_left_leg", "rear_left_foot", 
    "rear_right_shoulder", "rear_right_leg", "rear_right_foot"]

onMount(async () => {
    await cacheModelFiles()
    createScene()
    // await eig.ready;
    window.solve = solve

    sm = new SpotMicroKinematics(0, 0, 0, smnc_.smc)
    const idleBodyState:BodyState = {
        euler_angs: { phi:0, psi:0, theta:0},
        xyz_pos: { x:0, y: smnc_.lie_down_height, z:0 },
        leg_feet_pos: getNeutralStance()
    }
    sm.setBodyState(idleBodyState)
    const d2r = Math.PI / 180

    outControllerData.subscribe(data => {
        
        sm.setBodyAngles(0,data[1]/1000,0)
        //sm.setBodyState(idleBodyState);
        const joint_angs:LegsJointAngles = sm.getLegsJointAngles();
        const angles = [0,0,0,0,0,0,0,0,0,0,0,0]
        angles[0] = joint_angs.left_front.ang1
        angles[1] = joint_angs.left_front.ang2
        angles[2] = joint_angs.left_front.ang3

        angles[3] = joint_angs.right_front.ang1
        angles[4] = joint_angs.right_front.ang2
        angles[5] = joint_angs.right_front.ang3

        angles[6] = joint_angs.left_back.ang1
        angles[7] = joint_angs.left_back.ang2
        angles[8] = joint_angs.left_back.ang3

        angles[9] = joint_angs.right_back.ang1
        angles[10] = joint_angs.right_back.ang2
        angles[11] = joint_angs.right_back.ang3
        servoBuffer.set(angles)
    })

    let lastAngles = {}

    servoBuffer.subscribe(angles => {
        if(!robot || JSON.stringify(lastAngles) == JSON.stringify(angles)) return
        lastAngles = angles
        console.log("Got new agles", angles);
        
        for (let i = 0; i < servoNames.length; i++) {
            angles[i] = angles[i]//lerp(robot.joints[servoNames[i]].angle * (180/Math.PI), angles[i], 0.2)
            robot.joints[servoNames[i]].setJointValue(MathUtils.degToRad(angles[i]));
        }
    })
});

function homogTransXyz(x:number, y:number, z:number) {
  const matrix = mat4.create();

  mat4.translate(matrix, matrix, [x, y, z]);

  return matrix;
}

function homogRotXyz(x_ang: number, y_ang: number, z_ang: number) {
  const matrix = mat4.create();

  mat4.rotateX(matrix, matrix, x_ang);
  mat4.rotateY(matrix, matrix, y_ang);
  mat4.rotateZ(matrix, matrix, z_ang);

  return matrix;
}

function homogInverse(ht) {
  const temp_rot = mat3.create();
  mat3.fromMat4(temp_rot, ht);
  mat3.transpose(temp_rot, temp_rot);

  const temp_translate = vec3.create();
  vec3.set(temp_translate, -ht[12], -ht[13], -ht[14]);

  const ht_inverted1 = mat4.create();
  mat4.fromRotationTranslation(ht_inverted1, temp_rot, vec3.create());

  const ht_inverted2 = mat4.create();
  mat4.fromTranslation(ht_inverted2, temp_translate);

  const result = mat4.create();
  mat4.multiply(result, ht_inverted1, ht_inverted2);

  return result;
}

const htLegLeftFront = (body_length:number, body_width:number):mat4 => {
    const htLegLeftFront = homogRotXyz(0, -Math.PI/2, 0)
    htLegLeftBack[13] = body_length/2
    htLegLeftBack[14] = 0
    htLegLeftBack[15] = -body_width/2.0
    return htLegLeftFront
}

const htLegRightFront = (body_length:number, body_width:number):mat4 => {
    const htLegRightFront = homogRotXyz(0, -Math.PI/2, 0)
    htLegLeftBack[13] = body_length/2
    htLegLeftBack[14] = 0
    htLegLeftBack[15] = body_width/2.0
    return htLegRightFront 
}

const htLegLeftBack = (body_length:number, body_width:number):mat4 => {
    const htLegLeftBack = homogRotXyz(0, -Math.PI/2, 0)
    htLegLeftBack[13] = -body_length/2
    htLegLeftBack[14] = 0
    htLegLeftBack[15] = body_width/2.0
    return htLegLeftBack
}

const htLegRightBack = (body_length:number, body_width:number):mat4 => {
    const htLegRightBack = homogRotXyz(0, -Math.PI/2, 0)
    htLegLeftBack[13] = -body_length/2
    htLegLeftBack[14] = 0
    htLegLeftBack[15] = -body_width/2.0
    return htLegRightBack
}    

function ht0To1(rot_ang, link_length) {
  // Build up the matrix as from the paper
  const ht_0_to_1 = mat4.create();
  mat4.fromXRotation(ht_0_to_1, rot_ang);

  // Add in remaining terms
  ht_0_to_1[12] = -link_length * Math.cos(rot_ang);
  ht_0_to_1[13] = -link_length * Math.sin(rot_ang);

  return ht_0_to_1;
}

function ht1To2():mat4 {
  // Build up the matrix as from the paper
  const ht_1_to_2 = mat4.fromValues(
    0.0, 0.0, -1.0, 0.0,
    -1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 1.0
  );

  return ht_1_to_2;
}

function ht2To3(rot_ang: number, link_length: number):mat4 {
  // Build up the matrix as from the paper
  const ht_2_to_3 = mat4.create();
  mat4.fromXRotation(ht_2_to_3, rot_ang);

  // Add in remaining terms
  ht_2_to_3[12] = link_length * Math.cos(rot_ang);
  ht_2_to_3[13] = link_length * Math.sin(rot_ang);

  return ht_2_to_3;
}

function ht3To4(rot_ang: number, link_length: number):mat4 {
  // Same as the 2 to 3 transformation, so just call that function
  return ht2To3(rot_ang, link_length);
}

function ht0To4(joint_angles:JointAngles, link_lengths:LinkLengths):mat4 {
  // Result is a sequential multiplication of all 4 transform matrices
  const mat0To1 = ht0To1(joint_angles.ang1, link_lengths.l1);
  const mat2To3 = ht2To3(joint_angles.ang2, link_lengths.l2);
  const mat3To4 = ht3To4(joint_angles.ang3, link_lengths.l3);

  const result = mat4.create();
  mat4.multiply(result, mat0To1, ht1To2());
  mat4.multiply(result, result, mat2To3);
  mat4.multiply(result, result, mat3To4);

  return result;
}

const ikine = (point:Point, link_lengths:LinkLengths, is_leg_12:boolean):JointAngles => {
    let joint_angles:JointAngles = {ang1: 0, ang2: 0, ang3:0}

  // Convenience variables for math
    const x4 = point.x;
    const y4 = point.y;
    const z4 = point.z;
    const l1 = link_lengths.l1;
    const l2 = link_lengths.l2;
    const l3 = link_lengths.l3;
    
    // Supporting variable D
    let D = (x4*x4 + y4*y4 + z4*z4 - l1*l1 - l2*l2 - l3*l3) / (2*l2*l3);
    
    // Poor man's inverse kinematics reachability protection:
    // Limit D to a maximum value of 1, otherwise the square root functions
    // below (sqrt(1 - D^2)) will attempt a square root of a negative number
    if (D > 1.0) D = 1.0;
    else if (D < -1.0) D = -1.0

    joint_angles.ang3 = is_leg_12 ? Math.atan2(Math.sqrt(1 - D*D), D) : Math.atan2(-Math.sqrt(1 - D*D), D)

    // if (is_leg_12) joint_angles.ang3 = Math.atan2(Math.sqrt(1 - D*D), D);
    // else joint_angles.ang3 = Math.atan2(-Math.sqrt(1 - D*D), D);

    // Another poor mans reachability sqrt protection
    let protected_sqrt_val = x4*x4 + y4*y4 - l1*l1;
    if (protected_sqrt_val < 0) { protected_sqrt_val = 0;}

    joint_angles.ang2 = Math.atan2(z4, Math.sqrt(protected_sqrt_val)) -
    Math.atan2(l3*Math.sin(joint_angles.ang3), l2 + l3*Math.cos(joint_angles.ang3));

    joint_angles.ang1 = Math.atan2(y4, x4) + Math.atan2(Math.sqrt(protected_sqrt_val), -l1);

    return joint_angles;
} 

// wake (0, 0, 0, -40, -170, 0);
// sleep (0, 0, 0, 0, 0, 0)
const smnc_ = {
    smc:{
        hip_link_length: 0.055, // Straight line distance of the hip link (horizontal leg link)
        upper_leg_link_length: 0.1075, // Straight line distance of the upper leg link, joint to joint
        lower_leg_link_length: 0.130, // Straight line distance of the lower leg link, joint to joint
        body_width: 0.078, // Horizontal width between hip joints
        body_length: 0.186, // Length between shoulder joints
    },

    lie_down_height: 0.083, // Height of body center when sitting
    lie_down_feet_x_offset: 0.065
}

const getNeutralStance = (): LegsFootPos => {
    const len = smnc_.smc.body_length; // body length
    const width = smnc_.smc.body_width; // body width
    const l1 = smnc_.smc.hip_link_length; // length of the hip link
    const x_off = smnc_.lie_down_feet_x_offset;

    return  {
        left_front: {x:-len/2 + x_off, y:0, z:  width/2 - l1},
        right_front:{x: len/2 + x_off, y:0, z:  width/2 + l1},
        left_back:  {x: len/2 + x_off, y:0, z: -width/2 - l1},
        right_back: {x:-len/2 + x_off, y:0, z: -width/2 + l1}
    }
}

const getLieDownStance = (): LegsFootPos => {
    const len = smnc_.smc.body_length; // body length
    const width = smnc_.smc.body_width; // body width
    const l1 = smnc_.smc.hip_link_length; // length of the hip link
    const x_off = smnc_.lie_down_feet_x_offset;

    return  {
        left_front: {x: len/2 + x_off, y:0, z: -width/2 - l1},
        right_front:{x: len/2 + x_off, y:0, z:  width/2 + l1},
        left_back:  {x:-len/2 + x_off, y:0, z: -width/2 - l1},
        right_back: {x:-len/2 + x_off, y:0, z:  width/2 + l1}
    }
}
let sm:SpotMicroKinematics


const solve = (data) => {    
    //const kin = new kinematic(); 

    const phi   = 0//data[1]
    const theta = 0//data[1]//(data[1]- 127) / 30
    const psi   = 0//data[1]//(data[2]- 127) / 30
    const z     = 0//(data[3]- 127) / 30
    const y     = 0//data[1]//(data[4]- 127) / 30
    const x     = 0//y / z
    
    // const desired:JointAngles = {ang1: 10*d2r, ang2:20*d2r, ang3:-15*d2r}

    // const four_legs_desired_angs:LegsJointAngles = {left_front: desired, right_front: desired, left_back:desired, right_back:desired}

    // sm.setLegJointAngles(four_legs_desired_angs);

    // const test_legs_joint_angs:LegsJointAngles = sm.getLegsJointAngles();

    const d2r = Math.PI/180; 

    sm.setBodyState

      // Set an initial body height and stance cmd for idle mode


    //kin.calculate_foot_point()
    //kin.calculate_leg_positions()
}



interface Point {
  x:number
  y:number
  z:number
};

interface LinkLengths {
  l1:number
  l2:number
  l3:number
};

interface JointAngles {
  ang1:number
  ang2:number
  ang3:number
};

interface LegsJointAngles {
  right_back:JointAngles
  right_front:JointAngles
  left_front:JointAngles
  left_back:JointAngles
};

interface LegsFootPos {
  right_back:Point
  right_front:Point
  left_front:Point
  left_back:Point
};

// Struct to hold various configuration values of a spot micro robot frame
interface SpotMicroConfig {
  hip_link_length:number
  upper_leg_link_length:number
  lower_leg_link_length:number
  body_width:number
  body_length:number
};

interface EulerAngs {
    phi:number
    theta:number
    psi:number
}

interface BodyState {
  euler_angs:EulerAngs
  xyz_pos:Point
  leg_feet_pos:LegsFootPos
};

interface LegRelativeTransforms {
  t01:Matrix
  t13:Matrix
  t34:Matrix
};

interface AllRobotRelativeTransforms {
  bodyCenter:Matrix
  centerToRightBack:Matrix
  centerToRightFront:Matrix
  centerToLeftFront:Matrix
  centerToLeftBack:Matrix
  rightBackLeg:LegRelativeTransforms
  rightFrontLeg:LegRelativeTransforms
  leftFrontLeg:LegRelativeTransforms
  leftBackLeg:LegRelativeTransforms  
};

class SpotMicroLeg {
    private is_leg_12_:boolean
    private joint_angles_:JointAngles
    private link_lengths_:LinkLengths

    constructor(joint_angles:JointAngles, link_lengths:LinkLengths, is_leg_12:boolean) {
        this.joint_angles_ = joint_angles
        this.link_lengths_ = link_lengths
        this.is_leg_12_ = is_leg_12
    }

    setAngles(joint_angles:JointAngles) {
        this.joint_angles_ = joint_angles
    }

    get getAngles() {
        return this.joint_angles_
    }

    setFootPosGlobalCoordinates(point:Point, ht_leg_start:mat4) {
        // Need to express the point in the leg's coordinate system, can do so by
        // transforming a vector of the points in global coordinate by the inverse of
        // the leg's starting homogeneous transform

        // Make a homogeneous vector, and store the point in global coords in it
        const p4_ht_vec = vec4.create()
        p4_ht_vec[0] = point.x
        p4_ht_vec[1] = point.y
        p4_ht_vec[2] = point.z
        p4_ht_vec[3] = 1

        // Multiply it by the inverse of the homgeneous transform of the leg start.
        // This operation yields a foot position in the foot's local coordinates
        //p4_ht_vec = vec4.multiply() homogInverse(ht_leg_start) * p4_ht_vec; 

        //Point point_local{.x = p4_ht_vec(0), .y = p4_ht_vec(1), .z = p4_ht_vec(2)};
        
        vec4.transformMat4(p4_ht_vec, p4_ht_vec, homogInverse(ht_leg_start))

        const point_local:Point = {x:p4_ht_vec[0], y:p4_ht_vec[1], z:p4_ht_vec[2]}

        // Call this leg's method for setting foot position in local cordinates
        this.setFootPosLocalCoordinates(point_local);
    }

    setFootPosLocalCoordinates(point:Point) {
        const joint_angles:JointAngles = ikine(point, this.link_lengths_, this.is_leg_12_);

        // Call method to set joint angles of the leg
        this.setAngles(joint_angles);
    }

    getFootPosGlobalCoordinates(ht_leg_start:mat4): Point {
        // Get homogeneous transform of foot
        const ht_foot = mat4.multiply(mat4.create(), ht_leg_start, ht0To4(this.joint_angles_, this.link_lengths_))

        // Construct return point structure
        return {
            x: ht_foot[12],
            y: ht_foot[13],
            z: ht_foot[14],
        };
    }

    get getLegJointAngles(): JointAngles {
        return this.joint_angles_;
    }
}

class SpotMicroKinematics {
    private smc_:SpotMicroConfig
    private x_:number
    private y_:number
    private z_:number

    private phi_:number
    private theta_:number
    private psi_:number

    private right_back_leg_:SpotMicroLeg
    private right_front_leg_:SpotMicroLeg
    private left_front_leg_:SpotMicroLeg
    private left_back_leg_:SpotMicroLeg

    constructor(x:number, y:number, z:number, smc:SpotMicroConfig) {
        this.smc_ = smc
        this.x_ = x
        this.y_ = y
        this.z_ = z

        const joint_angles_temp:JointAngles = {ang1: 0, ang2: 0, ang3:0}
        const link_lengths_temp:LinkLengths = { l1: smc.hip_link_length, l2:smc.upper_leg_link_length, l3:smc.lower_leg_link_length}

        this.right_back_leg_  = new SpotMicroLeg(joint_angles_temp, link_lengths_temp, true);
        this.right_front_leg_ = new SpotMicroLeg(joint_angles_temp, link_lengths_temp, true);
        this.left_front_leg_  = new SpotMicroLeg(joint_angles_temp, link_lengths_temp, false);
        this.left_back_leg_   = new SpotMicroLeg(joint_angles_temp, link_lengths_temp, false);
    }

    getBodyHt(): mat4 { // : Matrix4f
        // Euler angle order is phi, psi, theta because the axes of the robot are x
        // pointing forward, y pointing up, z pointing right
        return mat4.multiply(mat4.create(), homogTransXyz(this.x_, this.y_, this.z_), homogRotXyz(this.phi_, this.psi_, this.theta_));
    }

    setFeetPosGlobalCoordinates(four_legs_foot_pos:LegsFootPos) {
        // Get the body center homogeneous transform matrix 
        const ht_body = this.getBodyHt();

        // Create each leg's starting ht matrix. Made in order of right back, right 
        //front, left front, left back
        const ht_rb = mat4.multiply(mat4.create(), ht_body, htLegRightBack(this.smc_.body_length, this.smc_.body_width))
        const ht_rf = mat4.multiply(mat4.create(), ht_body, htLegRightFront(this.smc_.body_length, this.smc_.body_width))
        const ht_lf = mat4.multiply(mat4.create(), ht_body, htLegLeftFront(this.smc_.body_length, this.smc_.body_width))
        const ht_lb = mat4.multiply(mat4.create(), ht_body, htLegLeftBack(this.smc_.body_length, this.smc_.body_width))


        // Call each leg's method to set foot position in global coordinates
        this.right_back_leg_.setFootPosGlobalCoordinates(four_legs_foot_pos.right_back, ht_rb);
        this.right_front_leg_.setFootPosGlobalCoordinates(four_legs_foot_pos.right_front, ht_rf);
        this.left_front_leg_.setFootPosGlobalCoordinates(four_legs_foot_pos.left_front, ht_lf);
        this.left_back_leg_.setFootPosGlobalCoordinates(four_legs_foot_pos.left_back, ht_lb);
    }

    setBodyState(body_state:BodyState) {
        this.x_ = body_state.xyz_pos.x; 
        this.y_ = body_state.xyz_pos.y; 
        this.z_ = body_state.xyz_pos.z;
        this.phi_ = body_state.euler_angs.phi;
        this.theta_ = body_state.euler_angs.theta;
        this.psi_ = body_state.euler_angs.psi;

        this.setFeetPosGlobalCoordinates(body_state.leg_feet_pos);
    }

    setBodyAngles(phi:number, theta:number, psi:number) {
        const saved_foot_pos:LegsFootPos = this.getLegsFootPos();

        this.theta_ = theta
        this.phi_ = phi
        this.psi_ = psi

        this.setFeetPosGlobalCoordinates(saved_foot_pos);
    }

    getLegsFootPos(): LegsFootPos {
  
    // Get the body center homogeneous transform matrix 
        const ht_body = this.getBodyHt()

        // Return the leg joint angles
        let ret_val:LegsFootPos = {left_back:{x:0,y:0,z:0}, left_front:{x:0,y:0,z:0}, right_back:{x:0,y:0,z:0}, right_front:{x:0,y:0,z:0}}

        // Create each leg's starting ht matrix. Made in order of right back, right 
        // front, left front, left back
        const ht_rb = mat4.multiply(mat4.create(), ht_body, htLegRightBack(this.smc_.body_length, this.smc_.body_width))
        const ht_rf = mat4.multiply(mat4.create(), ht_body, htLegRightFront(this.smc_.body_length, this.smc_.body_width))
        const ht_lf = mat4.multiply(mat4.create(), ht_body, htLegLeftFront(this.smc_.body_length, this.smc_.body_width))
        const ht_lb = mat4.multiply(mat4.create(), ht_body, htLegLeftBack(this.smc_.body_length, this.smc_.body_width))

        ret_val.right_back  = this.right_back_leg_.getFootPosGlobalCoordinates(ht_rb);
        ret_val.right_front = this.right_front_leg_.getFootPosGlobalCoordinates(ht_rf);
        ret_val.left_front  = this.left_front_leg_.getFootPosGlobalCoordinates(ht_lf);
        ret_val.left_back   = this.left_back_leg_.getFootPosGlobalCoordinates(ht_lb);
        return ret_val;
    }

    getLegsJointAngles(): LegsJointAngles {
        // Return the leg joint angles
        let ret_val:LegsJointAngles = {left_back:{ang1:0,ang2:0,ang3:0}, left_front:{ang1:0,ang2:0,ang3:0}, right_back:{ang1:0,ang2:0,ang3:0}, right_front:{ang1:0,ang2:0,ang3:0}}

        ret_val.right_back = this.right_back_leg_.getLegJointAngles;
        ret_val.right_front = this.right_front_leg_.getLegJointAngles;
        ret_val.left_front = this.left_front_leg_.getLegJointAngles;
        ret_val.left_back = this.left_back_leg_.getLegJointAngles;

        return ret_val;
    }
}

class kinematic {
    private omega = 0;
    private phi = 0;
    private psi = 0;
    private x = 0;
    private y = 0;
    private z = 0;

    private L = 207.5;
    private W = 78;
    private l1 = 60.5;
    private l2 = 10;
    private l3 = 100.7;
    private l4 = 118.5;

    private height = 200
    private p = [[],[],[],[]]
    private servo_angles = []

    calculate_foot_point = () => {

        const tan_omega = Math.tan(this.omega);
        const tan_psi = Math.tan(this.psi);
        // Front has impact of omega
        
        const h_offset = (this.W/2.0 + this.l1) * tan_omega;

        //Front Left Leg
        this.p[0][1]= - (this.height - h_offset);
        this.p[0][0] = -this.l1 + this.p[0][1]* tan_omega ;
        this.p[0][2] = this.p[0][1]* tan_psi;

        // Front Right leg
        this.p[1][1]= - (this.height + h_offset);
        this.p[1][0] = this.l1 - this.p[1][1]* tan_omega ;
        this.p[1][2] = this.p[1][1]* tan_psi;

        // Rear correct based on psi
        const height_rear = this.height + this.L * tan_psi;
        //Front Left Leg
        this.p[2][1]= - (height_rear - h_offset);
        this.p[2][0] = -this.l1 + this.p[2][1]* tan_omega;
        this.p[2][2] = this.p[2][1]* tan_psi;

        // Front Right leg
        this.p[3][1]= - (height_rear + h_offset);
        this.p[3][0] = this.l1 - this.p[3][1]* tan_omega ;
        this.p[3][2] = this.p[3][1] * Math.tan(this.psi);

        console.log(this.p)
    }

    calculate_leg_positions = () => {
        for (let l = 0; l < 4; l++) {
            this.leg_IK(this.p[l], l, this.servo_angles[l]);
            //console.log("IK (x,z,y) (%.1f, %.1f, %.1f) -> (%d, %d, %d) (%d)", p[l][0], p[l][2], p[l][1], servo_angles[l][0], servo_angles[l][1], servo_angles[l][2], ret);
        }
    }

    leg_IK = (p:number[], id:number, servo_angles:number[]) => {

    }
}


const cacheModelFiles = async () => {
    const cacheKey = "files"
    const cache = await caches.open(cacheKey)

    let data = await fetch("/stl.zip").then(data => data.arrayBuffer())
    
    var files = uzip.parse(data);
    
    for(const [path, data] of Object.entries(files) as [path:string, data:Uint8Array][]){
        const url = new URL(path, window.location.href)
        cache.put(url, new Response(data));   
    }    
}

const loadModel = () => {
    const url = '/spot_micro.urdf.xacro';
    const xacroLoader = new XacroLoader();
    xacroLoader.load( url, xml => {
        const urdfLoader = new URDFLoader();
        urdfLoader.workingPath = LoaderUtils.extractUrlBase( url );

        robot = urdfLoader.parse( xml );
        robot.rotation.x = Math.PI / 0.6666;
        robot.rotation.z = Math.PI / 2;
        robot.traverse(c => c.castShadow = true);
        robot.updateMatrixWorld(true);

        scene.add( robot );

    },  (error) =>  console.log(error));
}

const createScene = () => {
    scene = new Scene();
    camera = new PerspectiveCamera();
    camera.position.set(-0.5, 0.5, 1);

    renderer = new WebGLRenderer({ antialias: true, canvas: el });
    renderer.outputEncoding = sRGBEncoding;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = PCFSoftShadowMap;
    document.body.appendChild(renderer.domElement);

    const directionalLight = new DirectionalLight(0xffffff, 1.0);
    directionalLight.castShadow = true;
    directionalLight.shadow.mapSize.setScalar(1024);
    directionalLight.position.set(5, 30, 5);
    scene.add(directionalLight);

    const ambientLight = new AmbientLight(0xffffff, 0.2);
    scene.add(ambientLight);

    const ground = new Mesh(new PlaneGeometry(), new ShadowMaterial({ opacity: 0.25 }));
    ground.rotation.x = -Math.PI / 2;
    ground.scale.setScalar(30);
    ground.receiveShadow = true;
    scene.add(ground);

    controls = new OrbitControls(camera, renderer.domElement);
    controls.minDistance = 0;
    controls.maxDistance = 4;
    controls.update();

    loadModel()
    handleResize()
    render()
}


const handleResize = () => {
    renderer.setSize(window.innerWidth, window.innerHeight);
    renderer.setPixelRatio(window.devicePixelRatio);

    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
}

const render = () => {
    requestAnimationFrame(render);
    renderer.render(scene, camera);
}
</script>
  
<svelte:window on:resize={handleResize}></svelte:window>

<canvas bind:this={el} class="absolute"></canvas>
# AInimate: Animation Deployment Pipeline (ADP)
### Direct Mathematical Injection of AI-Generated Motion Data into Unreal Engine 5.6.1

**Project Status:** Phase 2 | Functional Proof-of-Concept  
**Role:** Stage 2 Core Developer (Skeletal Kinematics & C++ Engine Architecture)

---

## 🚀 The Mission: Bridging the "Deployment Gap"
While Generative AI can now synthesize human motion from text, a massive technical gap remains: **AI models output raw joint coordinates, but Game Engines require complex binary assets.** 

The **AInimate ADP** is a high-performance bridge that bypasses lossy FBX exports and manual retargeting. It takes raw mathematical motion parameters and performs **Direct Data Injection**—writing coordinate data directly into Unreal Engine's memory to author native `.uasset` animation sequences in $O(n)$ time.

---

## 📽️ Visual Validation (From AI to Asset)

| **Stage 1.5: Kinematic Validation** | **Stage 2: Native Engine Injection** |
| :---: | :---: |
| ![Matplotlib Visualization](media/python_viz.gif) | ![Unreal Engine Playback](media/ue5_playback.gif) |
| *Ground-truth solving using the **ACCAD Dataset*** | *Native .uasset injected and calibrated in UE5.6* |

---

## 📦 The Input: SMPL Motion Data
The pipeline begins with motion data represented via the **SMPL (Skinned Multi-Person Linear)** model. SMPL is the industry standard for academic human body representation but is natively incompatible with game engine skeletal structures.

*   **Learn more about SMPL:** [smpl.is.tue.mpg.de](https://smpl.is.tue.mpg.de)

### The `.npz` Data Contract
Our Stage 1 backend generates a compressed NumPy file (`input.npz`) containing:
*   **`global_orient`**: $[Frames, 3]$ — The world-space heading of the character.
*   **`body_pose`**: $[Frames, 69]$ — Local axis-angle rotations for 23 joints.
*   **`transl`**: $[Frames, 3]$ — Global $(X, Y, Z)$ translation vectors.
*   **`betas`**: $[10]$ — Shape coefficients defining the character's unique body proportions.

---

## 🛠️ Pipeline Stage 1: Python Pre-Processing (`smpl_converter.py`)
This module standardizes raw ML outputs into an "Engine-Ready" mathematical state.

*   **Coordinate Auto-Detection:** Utilizes a **Vertical Axis Dominance heuristic** to detect if source data is **Y-Up** (Standard SMPL) or **Z-Up** (AMASS/ACCAD) and applies the correct basis permutation.
*   **Root-Motion Splitting:** A specialized algorithm that "steals" horizontal displacement $(X, Y)$ from the pelvis and assigns it to a virtual **Root Bone**. The pelvis retains vertical $(Z)$ dynamics, enabling character navigation within Unreal’s physics system.
*   **Scalability via `bone_mapping.py`:** Rather than hardcoding bone names, the system uses a **Translation Dictionary**. This makes the project **skeleton-agnostic**; by simply updating this map, the pipeline can target any humanoid skeleton (MetaHumans, Mixamo, etc.) without changing a line of core logic.
*   **Ground Truth Validation (`visualize_npz.py`):** Before injection, we use this tool to solve the SMPL Forward Kinematics and render the motion in Matplotlib, ensuring the AI's output is physically sound.

---

## 🛠️ Pipeline Stage 2: C++ Injection Engine (`AInimateBPLibrary.cpp`)
Running natively within Unreal Engine 5.6.1, this backend performs the final bit-level asset authoring.

*   **Identity Calibration Algorithm ($Q_{\Delta}$):** SMPL data assumes a mathematical **T-Pose**, but Unreal characters use an **A-Pose**. The plugin solves the rotational delta in Global Space:
    $$Q_{\Delta} = (Q_{SMPL\_Identity})^{-1} \otimes Q_{UE\_Reference}$$
*   **Direct Memory Injection:** Utilizes the modern **`IAnimationDataController`** API. This allows us to "bite" data directly into the binary animation curves, ensuring **zero precision loss** compared to standard FBX imports.
*   **Recursive FK Solver:** A hierarchical solver that translates the corrected global transforms back into the parent-relative local keys required by the engine's `.uasset` format.

---

## 🧠 Engineering Challenge: Solving the "Spaghetti Monster"
A major hurdle was **Mesh Tearing** caused by mismatched bone lengths between the mathematical SMPL model and the artistic UE5 Mannequin. 

**The Solution:** I implemented a **Translation Locking Layer**. During the recursive solver, the code explicitly discards dynamic translations for all deformer bones, locking them to the Unreal Reference Skeleton's lengths while preserving the high-fidelity AI-generated rotations. This guaranteed 100% mesh integrity across all datasets.

| **Mismatched Lengths (Artifact)** | **Translation Locking (Fix)** |
| :---: | :---: |
| ![Spaghetti Error](media/spaghetti_fix_before.png) | ![Clean Fix](media/spaghetti_fix_after.png) |

---

## 📊 The Data Bridge (JSON)
The stages communicate via a high-precision JSON bridge. This decoupled architecture allows the AI to be hosted on high-end GPU servers while the plugin remains lightweight for the developer's workstation.

```json
{
  "meta": {
    "calibration_pose": { "pelvis": {"location": [0,0,89], "rotation": [0,0,0,1]} },
    "frame_rate": 30
  },
  "frames": [
    { "bone_transforms": { "pelvis": {"location": [150, 200, 92], "rotation": [x,y,z,w]} } }
  ]
}
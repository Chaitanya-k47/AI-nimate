# AInimate: Animation Deployment Pipeline (ADP)
### Direct Mathematical Injection of AI-Generated Motion into Unreal Engine 5.6

**Project Status:** Phase 2 | Functional Proof-of-Concept  
**Role:** Stage 2 Core Developer (Python Pre-processing & C++ Engine Architecture)

---

## 🚀 Overview
**AInimate** is an end-to-end framework designed to bridge the "Deployment Gap" between Generative AI research and professional game engines. While Stage 1 of the project (Generative Backend) focuses on synthesizing SMPL joint trajectories via Diffusion models, this repository contains **Stage 2: The Animation Deployment Pipeline**.

Traditional workflows rely on lossy FBX imports and manual retargeting. This pipeline bypasses those bottlenecks by performing **Direct Data Injection**—writing mathematical coordinate data directly into the Unreal Engine binary memory to create native `.uasset` animation sequences in $O(n)$ time.

---

## 📽️ Visual Validation (Input vs. Output)

| **Stage 1.5: Ground Truth (Python)** | **Stage 2: Native Injection (UE5)** |
| :---: | :---: |
| ![Matplotlib Visualization]([LINK_TO_PYTHON_GIF_OR_IMAGE]) | ![Unreal Engine Playback]([LINK_TO_UNREAL_GIF_OR_IMAGE]) |
| *Raw SMPL parameter solving in Matplotlib* | *Native .uasset injected and calibrated in UE5* |

> **[PRO-TIP: PLACE A SIDE-BY-SIDE COMPARISON IMAGE OR GIF HERE]**
> *Create a GIF showing the blue stick figure in Python moving, next to the UE5 Mannequin doing the same move.*

---

## 🛠️ Key Engineering Pillars

### 1. Python Pre-Processor (`smpl_converter.py`)
A dataset-agnostic module that standardizes raw ML outputs for the engine environment.
*   **Coordinate Swizzling:** Automatically detects source orientation (Y-Up vs. Z-Up) using a Vertical Axis Dominance heuristic and performs a Basis Permutation to align with Unreal’s +Y Forward convention.
*   **Root-Motion Splitting:** Decouples horizontal world translation ($X, Y$) from vertical skeletal height ($Z$). This assigns movement to the virtual Root bone, making AI motion immediately "Game-Ready" for navigation.
*   **Unit Scaling:** Resolves the discrepancy between SMPL (Meters) and Unreal (Centimeters).

### 2. C++ Injection Engine (`AInimateBPLibrary.cpp`)
The performance-critical backend running natively within Unreal Engine.
*   **Direct Memory Injection:** Utilizes the modern **`IAnimationDataController`** API to author animation curves directly in memory, ensuring zero data loss and $O(n)$ latency.
*   **Identity Calibration Algorithm ($Q_{\Delta}$):** Mathematically aligns the SMPL T-Pose with the Unreal A-Pose in Global Space. 
    *   *Formula:* $Q_{\Delta} = (Q_{SMPL\_Identity})^{-1} \otimes Q_{UE\_Reference}$
*   **Recursive FK Solver:** Translates corrected global transforms back into the hierarchical local space required by the engine's curve format.

---

## 🧠 Technical Problem Solving (The "Spaghetti Monster" Fix)

One of the primary challenges was **Mesh Tearing** caused by mismatched bone lengths between the mathematical SMPL model and the artistic UE5 Mannequin.

**The Solution:** I implemented a **Translation Locking Layer** within the recursive FK solver. By explicitly discarding dynamic translations for all bones except the `root` and `pelvis`, and forcing them to the engine's Reference Pose lengths, we achieved 100% mesh integrity while preserving high-fidelity AI rotations.

> **[INSERT IMAGE HERE]**
> *Compare a screenshot of a "stretched" character vs. your final "locked" character.*

---

## 📊 The Data Contract (JSON Bridge)
The stages communicate via a high-precision JSON bridge, ensuring the ML backend remains decoupled from the Engine frontend.

```json
{
  "meta": {
    "calibration_pose": { "pelvis": {"location": [0,0,89], "rotation": [x,y,z,w]} }
  },
  "frames": [
    { "bone_transforms": { "pelvis": {"location": [150, 200, 92], "rotation": [x,y,z,w]} } }
  ]
}
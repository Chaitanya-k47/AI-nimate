#this file contains the main logic and imports the BONE_MAP from the bone_mapping.py file.
#to activate virtual environment: .\venv\Scripts\activate
#then run: cd unreal/AInimate/Plugins/AInimatePlugin/Content/PythonBackend

import os
import sys
import torch
import smplx
import numpy as np
from scipy.spatial.transform import Rotation as R
import json

# Import the BONE_MAP from bone_mapping.py
from bone_mapping import BONE_MAP

#----------------CONSTANTS AND CONFIGURATION------------------
#smpl model path
MODEL_PATH = './smpl_models'

#model gender
GENDER =  'neutral'

#device to run the computations on.
DEVICE = torch.device('cpu')


#----------------GLOBAL SETUP------------------
#creating SMPL model and loading it onto the device.
print("Loading SMPL model...")

smpl_model = smplx.create(
    model_path=MODEL_PATH,
    model_type='smpl',
    gender=GENDER,
    use_pca=False, #full joint rotation data, not just compressed PCA data.
    flat_hand_mean=True #use flat hand mean pose(T pose with straight fingers).
).to(DEVICE)

print("SMPL model loaded successfully.")


def apply_ue_swizzle(pos, quat, is_z_up):
    """
    Converts RH (Y-Up or Z-Up) to LH Z-Up with +Y Forward.
    pos/quat: list or ndarray [x, y, z] / [x, y, z, w]
    """
    x, y, z = pos
    qx, qy, qz, qw = quat

    # Logic: We flip the X-axis to change from Right-Handed to Left-Handed.
    # To maintain movement direction: Input +Y (Forward) -> Unreal +Y (Forward).
    if is_z_up:
        # DATASET: RH Z-Up (X=Right, Y=Forward, Z=Up)
        new_pos = [-x, y, z]
        # Quat reflection across YZ plane: Keep X, negate Y and Z
        new_quat = [qx, -qy, -qz, qw]
    else:
        # DATASET: RH Y-Up (X=Left, Y=Up, Z=Forward)
        # We need Y to become Z (Up) and Z to become Y (Forward)
        new_pos = [-x, z, y]
        new_quat = [qx, qz, qy, qw]
        
    return new_pos, new_quat

# ----------------MAIN CONVERSION FUNCTION------------------
def convert_smpl_to_ue_json(smpl_output_data: dict) -> dict:
    poses_np = np.array(smpl_output_data['poses'])
    trans_np = np.array(smpl_output_data['trans'])
    num_frames = poses_np.shape[0]
    
    # Extract tensors dynamically
    global_orient = torch.tensor(poses_np[:, :3], dtype=torch.float32).to(DEVICE)
    body_pose = torch.tensor(poses_np[:, 3:], dtype=torch.float32).to(DEVICE)
    
    raw_betas = smpl_output_data['betas']
    betas = torch.tensor(raw_betas, dtype=torch.float32).to(DEVICE)
    if betas.ndim == 1:
        betas = betas.unsqueeze(0)
    elif betas.ndim == 2 and betas.shape[0] != 1:
        betas = betas[0].unsqueeze(0)

    # 1. --- GENERATE WORLD JOINTS ---
    with torch.no_grad():
        model_output = smpl_model(
            betas=betas,
            body_pose=body_pose,
            global_orient=global_orient,
            return_verts=False
        )
    joints_world = model_output.joints.cpu().numpy() # [Frames, 24, 3]
    parents = smpl_model.parents.cpu().numpy()

    # 2. --- AUTO-DETECT COORDINATE SYSTEM ---
    up_vec = joints_world[0, 15] - joints_world[0, 0] 
    IS_Z_UP_DATA = abs(up_vec[2]) > abs(up_vec[1])
    print(f">> Detected {'Z-UP' if IS_Z_UP_DATA else 'Y-UP'} system.")

    # 3. --- CALIBRATION POSE (DYNAMIC SHAPE) ---
    calibration_pose = {}
    with torch.no_grad():
        # DYNAMICALLY match the shape of the input body_pose
        t_body = torch.zeros((1, body_pose.shape[1]), dtype=torch.float32).to(DEVICE)
        t_orient = torch.zeros((1, 3), dtype=torch.float32).to(DEVICE)
        
        t_output = smpl_model(betas=betas, body_pose=t_body, global_orient=t_orient, return_verts=False)
        t_joints = t_output.joints.cpu().numpy()[0]
    
    for smpl_idx, ue_name in BONE_MAP.items():
        # Pure Identity T-Pose [0,0,0,1] swizzled to Unreal Space
        p, q = apply_ue_swizzle(t_joints[smpl_idx], [0,0,0,1], IS_Z_UP_DATA)
        calibration_pose[ue_name] = {
            "location": [val * 100 for val in p], 
            "rotation": q
        }
    calibration_pose["root"] = {"location": [0,0,0], "rotation": [0,0,0,1]}

    # 4. --- PROCESS ANIMATION GLOBAL ROTATIONS ---
    full_pose_np = poses_np.reshape(num_frames, -1, 3)
    num_joints_in_data = full_pose_np.shape[1]
    global_rotations = np.zeros((num_frames, num_joints_in_data, 4))

    for i in range(num_joints_in_data):
        local_rot = R.from_rotvec(full_pose_np[:, i, :])
        parent_idx = parents[i] if i < len(parents) else -1
        
        if parent_idx == -1:
            # ### FIX: Removed the hardcoded 90-degree pitch. 
            # Since the visualizer shows the character standing, 
            # the global_orient already handles the standing-up logic.
            global_rotations[:, i, :] = local_rot.as_quat()
        else:
            parent_global_quat = global_rotations[:, parent_idx, :]
            parent_r = R.from_quat(parent_global_quat)
            total_r = parent_r * local_rot
            global_rotations[:, i, :] = total_r.as_quat()

    # 5. --- GROUNDING ---
    frame_0_joints_abs = joints_world[0] + trans_np[0]
    height_offset = -np.min(frame_0_joints_abs[:, 2 if IS_Z_UP_DATA else 1])

    # 6. --- FRAME GENERATION ---
    output_frames = []
    for f in range(num_frames):
        bone_transforms = {}
        root_xy_cm = [0.0, 0.0]

        for smpl_idx, ue_name in BONE_MAP.items():
            # Absolute World Position
            raw_pos = joints_world[f, smpl_idx] + trans_np[f]
            raw_quat = global_rotations[f, smpl_idx]
            
            p, q = apply_ue_swizzle(raw_pos, raw_quat, IS_Z_UP_DATA)
            p[2] += height_offset + 0.02 

            if smpl_idx == 0: # Pelvis
                # Store the World XY for the Root Bone
                root_xy_cm = [p[0] * 100, p[1] * 100]
                
                # Pelvis retains full World Position for C++ relative math
                bone_transforms[ue_name] = {
                    "location": [p[0]*100, p[1]*100, p[2]*100], 
                    "rotation": q
                }
            else:
                bone_transforms[ue_name] = {
                    "location": [val * 100 for val in p], 
                    "rotation": q
                }
        
        # Inject the Root Bone (Handles XY sliding)
        bone_transforms["root"] = {
            "location": [root_xy_cm[0], root_xy_cm[1], 0.0], 
            "rotation": [0, 0, 0, 1]
        }
        
        output_frames.append({
            "frame_number": f, 
            "bone_transforms": bone_transforms
        })

    return {
        "meta": {
            "frame_rate": smpl_output_data.get('frame_rate', 30), 
            "total_frames": num_frames, 
            "calibration_pose": calibration_pose
        },
        "frames": output_frames
    }
    
    

if __name__ == '__main__':
    
    #----HELPER FUNCTION: write debug file. ----
    def write_debug_file(data_dict, filename):
        """Writes the contents of a dictionary (arrays) to a text file."""
        print(f"Writing debug content to: {filename}...")
        
        # Set numpy to print everything without ... truncation
        np.set_printoptions(threshold=sys.maxsize)
        
        with open(filename, 'w') as f:
            f.write(f"DEBUG DATA DUMP\n")
            f.write("=" * 30 + "\n\n")
            
            for key, value in data_dict.items():
                f.write(f"--- Key: {key} ---\n")
                
                # Handle numpy arrays
                if isinstance(value, np.ndarray):
                    f.write(f"Shape: {value.shape}\n")
                    f.write(f"Data Type: {value.dtype}\n")
                    f.write("Data:\n")
                    f.write(np.array2string(value, separator=', '))
                else:
                    # Handle primitives (int/float)
                    f.write(str(value))
                    
                f.write("\n\n" + "="*30 + "\n\n")
   
    
    #----HELPER FUNCTION: Generate Dummy Data ----
    def generate_dummy_data(num_frames=60):
        """Creates a sample input dictionary with random motion."""
        print(f"Generating dummy SMPL data for {num_frames} frames...")
        
        dummy_data = {
            'poses': np.zeros((num_frames, 72), dtype=np.float32), 
            'trans': np.zeros((num_frames, 3), dtype=np.float32), 
            'betas': np.zeros(10, dtype=np.float32), 
            'frame_rate': 30
        }
        
        return dummy_data


    #----HELPER FUNCTION: Load from .npz file ----
    def load_from_npz(filepath):
        """loads data from .npz file and structures it for converter."""    
        print(f"Opening .npz file: {filepath}")
        
        #load from file
        data = np.load(filepath)
                
        #2.EXTRACT: get data for converter
        """
            'global_orient' -> Root rotation [Frames, 3]
            'body_pose'     -> Body rotation [Frames, 63 or 66] (21 or 22 joints * 3)
            'transl'        -> Global translation(Root transl) [Frames, 3]
            'betas'         -> Shape params [10]
        """
        try:
            """
                Combine global_orient and body_pose to make full 'poses' array [Frames, 66] or [Frames, 72]
                npz output shows body_pose is (60, 63) or (60, 66) and global_orient is (60, 3)
                Concatenate along axis 1 (columns)
                63 + 3 = 66 parameters. (22 joints) or 66 + 3 = 69 parameters (23 joints)
                
                Note: Standard SMPL has 24 joints (72 params). 
                If model outputs less than 24 joints, we pad the remaining joints with zeros to reach 72.
                Whatever the model outputs, we add 1 root joint to it i.e. [global_orient].
            """
            
            if 'poses' in data.files:
                # Standard AMASS format: Everything is in one 'poses' array
                full_poses = data['poses']
                # AMASS sometimes has 156 parameters (SMPL+H with hands). We only want the first 72.
                if full_poses.shape[1] > 72:
                    full_poses = full_poses[:, :72]
            elif 'global_orient' in data.files and 'body_pose' in data.files:
                # Processed format: Split into root and body
                global_orient = data['global_orient']
                body_pose = data['body_pose']
                full_poses = np.concatenate((global_orient, body_pose), axis=1) 
            else:
                raise KeyError("Could not find 'poses' or 'global_orient'/'body_pose' in the NPZ file.")
            
            #pad wid zeros to reach 72 columns/parameters if needed
            current_cols = full_poses.shape[1]
            if current_cols < 72:
                padding = np.zeros((full_poses.shape[0], 72 - current_cols))
                full_poses = np.concatenate((full_poses, padding), axis=1)
                
            #extract translation
            if 'transl' in data.files:
                trans = data['transl']
            elif 'trans' in data.files:
                trans = data['trans']
            else:
                #default zero translation if missing
                trans = np.zeros((full_poses.shape[0], 3))
                
            #extract betas
            if 'betas' in data.files:
                raw_betas = data['betas']
                
                # Check if it's 2D (Frames, Betas) like (60, 10)
                if raw_betas.ndim == 2:
                    # Take the first frame's betas. Body shape is constant.
                    betas = raw_betas[0]
                else:
                    betas = raw_betas

                # Ensure we only take the first 10 shape parameters
                if betas.shape[0] > 10:
                    betas = betas[:10]
            else:
                betas = np.zeros(10)
                
            #Construct dictionary
            formatted_data = {
                'poses': full_poses,
                'trans': trans,
                'betas': betas,
                'frame_rate': 30 #defaulting to 30 as NPZ usually doesn't store framerate
            }
            
            return formatted_data
        
        except KeyError as e:
            print(f"Error parsing NPZ structure. Missing expected key: {e}")
            raise e


    # --- INPUT LOGIC ---
    # Define the name of the input file
    input_npz_filename = "input.npz"
    
    # Get directory of this script
    script_dir = os.path.dirname(__file__)
    
    # Path to the input file
    input_npz_filepath = os.path.join(script_dir, input_npz_filename)

    test_smpl_data = {}

# Logic: Check for NPZ first. If not found, Fallback to Dummy.
    if os.path.exists(input_npz_filepath):
        # CASE 1: NPZ File Found
        print(f"STATUS: NPZ Input file '{input_npz_filename}' found.")
        try:
            test_smpl_data = load_from_npz(input_npz_filepath)
            debug_path = os.path.join(script_dir, "input_debug.txt")
            write_debug_file(test_smpl_data, debug_path)
            print("NPZ data loaded and parsed successfully.")
        except Exception as e:
            print(f"ERROR: Failed to load/parse NPZ file. Reason: {e}")
            exit(1)
    else:
        # CASE 2: File Not Found (Fallback)
        print(f"STATUS: Input file '{input_npz_filename}' NOT found.")
        print("Falling back to generating dummy data...")
        test_smpl_data = generate_dummy_data(num_frames=90)
        debug_path = os.path.join(script_dir, "dummy_debug.txt")
        write_debug_file(test_smpl_data, debug_path)

    # 2. Run our conversion function
    print("Running SMPL to UE conversion...")
    ue_json_output = convert_smpl_to_ue_json(test_smpl_data)
    print("Conversion complete.")

    # 3. Save the output to the Unreal Project folder
    target_dir = script_dir # Save it right next to the script!
    
    output_filepath = os.path.join(target_dir, "test_output.json")

    with open(output_filepath, 'w') as f:
        json.dump(ue_json_output, f, indent=4)

    print(f"\nSuccessfully generated animation data.")
    print(f"Output saved to: {output_filepath}")
    print(f"Total frames: {ue_json_output['meta']['total_frames']}")
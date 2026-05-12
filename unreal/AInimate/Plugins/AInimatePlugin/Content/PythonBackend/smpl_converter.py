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


#----------------MAIN CONVERSION FUNCTION------------------
def convert_smpl_to_ue_json(smpl_output_data: dict) -> dict:
    # --- CONFIGURATION ---
    # Set to True for AMASS/ACCAD (Z-Up). Set to False for Standard SMPL (Y-Up).
    IS_Z_UP_DATA = True 

    poses_np = np.array(smpl_output_data['poses'])
    trans_np = np.array(smpl_output_data['trans'])
    num_frames = poses_np.shape[0]

    output_frames =[] 
    for frame_idx in range(num_frames):
        bone_transforms = {}
        
        # 1. Handle Global Translation (Root Motion)
        root_trans = trans_np[frame_idx] 
        if IS_Z_UP_DATA:
            # AMASS: +Y is Forward, +X is Right. UE: +X is Forward, +Y is Right.
            ue_root_loc = [root_trans[1] * 100.0, root_trans[0] * 100.0, root_trans[2] * 100.0]
        else:
            # SMPL: +Z is Forward, +X is Left. UE: +X is Forward, +Y is Right.
            ue_root_loc = [root_trans[2] * 100.0, -root_trans[0] * 100.0, root_trans[1] * 100.0]

        # 2. Handle Local Bone Rotations
        for smpl_idx, ue_name in BONE_MAP.items():
            
            # Extract the local axis-angle for this specific bone
            start_col = smpl_idx * 3
            end_col = start_col + 3
            local_axis_angle = poses_np[frame_idx, start_col:end_col]
            
            # Convert to Quaternion (x, y, z, w)
            local_quat = R.from_rotvec(local_axis_angle).as_quat()
            
            # --- THE MATHEMATICALLY PERFECT SWIZZLE (From the PDF) ---
            if IS_Z_UP_DATA:
                # AMASS (Z-up, Right-Handed) to UE5 (Z-up, Left-Handed)
                # Swap X and Y, negate vectors to flip handedness.
                ue_quat = [-local_quat[1], -local_quat[0], -local_quat[2], local_quat[3]]
            else:
                # SMPL (Y-up, Right-Handed) to UE5 (Z-up, Left-Handed)
                ue_quat = [-local_quat[2], local_quat[0], -local_quat[1], local_quat[3]]

            # Local translation is ALWAYS 0 for bones, except the Pelvis/Root
            loc = [0.0, 0.0, 0.0]
            if smpl_idx == 0: # Pelvis/Root
                loc = ue_root_loc
                
            bone_transforms[ue_name] = {
                "location": loc, 
                "rotation": ue_quat
            }
        
        output_frames.append({
            "frame_number": frame_idx,
            "bone_transforms": bone_transforms
        })

    final_output = {
        "meta": { "frame_rate": smpl_output_data.get('frame_rate', 30), "total_frames": num_frames },
        "frames": output_frames 
    }
    return final_output
    

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
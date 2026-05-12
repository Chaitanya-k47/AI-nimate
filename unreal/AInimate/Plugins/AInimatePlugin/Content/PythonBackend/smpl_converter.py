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
    """
    takes raw SMPL parameters from ML model and converts them into UE-compatible JSON format
    with local bone transform.
    
    args:
        smpl_output_data (dict): A dictionary containing 'poses', 'trans', 'betas', 'frame_rate', etc.
    
    returns:
        dict: A dictionary formatted for Unreal Engine with local bone transforms.
    
    """
    
    IS_Z_UP_DATA = True 
    
    #smpl_output_data is a dictionary of raw SMPL parameters from ML model.
    #it contains the keys: 'poses', 'betas', 'trans', 'frame_rate', etc.
    #extract this data into arrays and then convert it into tensors, for pytorch to process.
    poses_np = np.array(smpl_output_data['poses'])
    betas_np = np.array(smpl_output_data['betas'])
    trans_np = np.array(smpl_output_data['trans'])
    
    #poses_np shape: [num_frames, 66] (column index 0-2: root bone pose, column index 3-65: body pose), for each row(frame).
    num_frames = poses_np.shape[0]
    #betas_np shape: [10,] (only first 10 are used for SMPL, rest are ignored) 1D array
    #trans_np shape: [num_frames, 3]

    #converting numpy arrays to torch tensors
    global_orient = torch.tensor(poses_np[:, :3], dtype=torch.float32).to(DEVICE) #poses for root bone.
    body_pose = torch.tensor(poses_np[:, 3:], dtype=torch.float32).to(DEVICE)

    if betas_np.ndim == 2:
        betas_np = betas_np.flatten()
    betas = torch.tensor(betas_np, dtype=torch.float32).unsqueeze(0).to(DEVICE)
    
    #transl = torch.tensor(trans_np, dtype=torch.float32).to(DEVICE)
    #transl: [num_frames, 3]
    
    #betas require a batch dimension, to make it a 2D tensor.
    #created tensors are 2D and have shapes:
    #global_orient: [num_frames, 3]
    #body_pose: [num_frames, 63]
    #betas: [1, 10] (a batch dimension is added)
    
    with torch.no_grad():
        #gradient not needed we just doing inference
        model_output = smpl_model(
            betas=betas,
            body_pose=body_pose,
            global_orient=global_orient,
            return_verts=False,
            return_full_pose=False
        )
    
    """
    Not passing transl to the smpl_model() forces model_output.joints to return
    joints in model space coordinates i.e. all the joints are relative to the root joint,
    the root joint being at the origin (0,0,0).
        OR
    The other way to look at it is that the root joints's position in world space is at
    world origin (0,0,0). So all other joints are relative to that.     
    
    Hence,
        we keep the model's root at world origin (0,0,0) and get all joints coordinates
        relative to the root joint then we calculate local translation for every joint
        relative to its parent joint.
        Then we handle the root motion separately.
    
    model_output.joints has shape: [1, num_frames, num_joints=24, 3]
    """
    
    #Get the world space 3D joint positions(considering the model root at world origin) from the output
    joints_world = model_output.joints.cpu().numpy()
    #joints_world shape: [num_frames, num_joints=24, 3]
    
    """
    The smpl_model.parents is an array that stores the entire heirarchy of the SMPL model.
    Its a list where:
        The index of an item represents a child joint's ID.
        And the value at that index represents the parent joint's ID.
    
    example: parents = [-1, 0, 0, 1, 2, ...]
        parents[1] = 0, means joint with ID 1 has parent joint with ID 0.   
        parents[2] = 0, means joint with ID 2 has parent joint with ID 0.
        parents[15] = 12, means joint with ID 15 has parent joint with ID 12.
        
        parents[0] = -1, means joint with ID 0 is the root of the skeleton/model.
        i.e. the bone with parent[index] = -1 is the root bone, since it has no parent.  
    """
    #Get the parent of each joint to calculate local translation
    #SMPL model hierarchy is fixed. Parent index -1 means root of skeleton/model.
    parents = smpl_model.parents.cpu().numpy()
    #parents shape: [num_joints=24,] 1D array
    
    #AUTO-DETECT COORDINATE SYSTEM (Y-UP vs Z-UP) ---
    #We calculate the vector from the Pelvis (Joint 0) to the Head (Joint 15) at Frame 0.
    # Whichever axis has the largest distance is the "Up" axis for this specific dataset.
    pelvis_pos_f0 = joints_world[0, 0]
    head_pos_f0 = joints_world[0, 15]
    up_vector = head_pos_f0 - pelvis_pos_f0
    
    # Compare the absolute magnitude of the Y and Z axes
    if abs(up_vector[2]) > abs(up_vector[1]):
        IS_Z_UP_DATA = True
        print(f"Auto-Detected System: Z-UP (Up Vector: {up_vector})")
    else:
        IS_Z_UP_DATA = False
        print(f"Auto-Detected System: Y-UP (Up Vector: {up_vector})")
    
    
    #GENERATE THE PURE MATHEMATICAL T-POSE FOR CALIBRATION
    with torch.no_grad():
        tpose_orient = torch.zeros((1, 3), dtype=torch.float32).to(DEVICE)
        tpose_body = torch.zeros((1, body_pose.shape[1]), dtype=torch.float32).to(DEVICE)
        tpose_output = smpl_model(
            betas=betas, body_pose=tpose_body, global_orient=tpose_orient,
            return_verts=False, return_full_pose=False
        )
        
    tpose_joints_world = tpose_output.joints.cpu().numpy()
    tpose_global_rotations = np.zeros((1, 24, 4)) #single frame, 24 joints, quaternion (4)
    
    for i in range(24):
        parent_idx = parents[i]
        local_rot_obj = R.from_rotvec([0, 0, 0])
        if parent_idx == -1:
            tpose_global_rotations[0, i, :] = local_rot_obj.as_quat()
        else:
            parent_global_quat = tpose_global_rotations[0, parent_idx, :]
            total_r = R.from_quat(parent_global_quat) * local_rot_obj
            tpose_global_rotations[0, i, :] = total_r.as_quat()
    
    calibration_pose = {}
    for smpl_idx, ue_name in BONE_MAP.items():
        raw_pos = tpose_joints_world[0, smpl_idx] 
        raw_quat = tpose_global_rotations[0, smpl_idx] 
        
        if IS_Z_UP_DATA:
            ue_pos = [raw_pos[1], raw_pos[0], raw_pos[2]] 
            ue_quat = [-raw_quat[1], -raw_quat[0], -raw_quat[2], raw_quat[3]]
        else:
            ue_pos =[raw_pos[0], -raw_pos[2], raw_pos[1]]
            ue_quat =[raw_quat[0], -raw_quat[2], raw_quat[1], raw_quat[3]]
        
        calibration_pose[ue_name] = {
            "location":[p * 100 for p in ue_pos], 
            "rotation": ue_quat
        }
        
    #also adding dummy root to calibration pose just for structural safety
    calibration_pose["root"] = { "location":[0.0, 0.0, 0.0], "rotation":[0.0, 0.0, 0.0, 1.0]}  
        
        
    #reshape poses_np for easier processing
    #full pose is the global orientation + body pose
    full_pose_np = poses_np.reshape(num_frames, -1, 3)
    #full_pose_np shape: [num_frames, num_joints=24, 3]
    # -1 here means infer from other dimensions.
    
    #we need to store Global Rotations (Quaternions) for all 24 bones
    #Shape: [Frames, 24, 4] (x,y,z,w)
    global_rotations = np.zeros((num_frames, 24, 4))
    
    #Iterate through bones in hierarchical order (0 is root, others follow)
    #SMPL bone indices are already sorted by hierarchy
    for i in range(24):
        parent_idx = parents[i]
        
        #convert current bone's local axis-angle to Quat (Scipy is (x, y, z, w))
        local_axis_angle = full_pose_np[:, i, :]
        local_rot_obj = R.from_rotvec(local_axis_angle)
        
        if parent_idx == -1:
            # Root Bone: Global is just Local
            global_rotations[:, i, :] = local_rot_obj.as_quat()
        else:
            # Child Bone: Global = Parent_Global * Local
            parent_global_quat = global_rotations[:, parent_idx, :]
            
            # Scipy rotation multiplication
            parent_r = R.from_quat(parent_global_quat)
            total_r = parent_r * local_rot_obj
            global_rotations[:, i, :] = total_r.as_quat()
            
     # --- 2. CALCULATE HEIGHT OFFSET (DYNAMIC AUTO-GROUNDING) ---
    # We look at Frame 0. We find the lowest Y or Z value (depending on the coordinate system).
    # We calculate how much we need to shift up to make that lowest point 0.
    
    # SMPL joints: 0-Pelvis, 10-Left Foot/Toe, 11-Right Foot/Toe (approximate indices)
    # Ideally we scan all joints to find the absolute floor.
    frame_0_joints = joints_world[0] + trans_np[0] # Apply root trans to get absolute pos
    
    if IS_Z_UP_DATA:
        min_height = np.min(frame_0_joints[:, 2]) # Z is Up
        height_offset = -min_height
        print(f"Auto-Grounding (Z-Up): Shifting character UP by {height_offset*100:.2f} cm")
    else:
        min_height = np.min(frame_0_joints[:, 1]) # Y is Up
        height_offset = -min_height
        print(f"Auto-Grounding (Y-Up): Shifting character UP by {height_offset*100:.2f} cm")
    
    #loop through frames and bones to create the final JSON structure.
    output_frames = [] #list(ordered) of frames. each frame is a dictionary of bone transforms.
    for frame_idx in range(num_frames):
        bone_transforms = {}
        
        # Get Root Translation for this frame (from input data)
        # This acts as the offset for the whole character in World Space
        root_trans_vec = trans_np[frame_idx] 

        for smpl_idx, ue_name in BONE_MAP.items():
            
            # --- POSITIONS (Global) ---
            # Start with SMPL joint position (which is relative to root)
            # Add the Root Translation to place it truly in World Space
            raw_pos = joints_world[frame_idx, smpl_idx] + root_trans_vec
            
            # --- ROTATIONS (Global) ---
            # Get the calculated Global Quaternion
            raw_quat = global_rotations[frame_idx, smpl_idx] # (x, y, z, w)
            
            # --- DYNAMIC SWIZZLING ---
            if IS_Z_UP_DATA:
                # Data is already Z-Up. 
                corrected_z = raw_pos[2] + height_offset + 0.02
                
                #We swap X and Y to convert Right-Handed to Left-Handed and face X-Forward.
                ue_pos = [raw_pos[1], raw_pos[0], corrected_z]
                #ue_pos = [raw_pos[0], raw_pos[1], corrected_z]
                
                #Swap X and Y, AND negate the vector parts to flip Handedness!
                ue_quat = [-raw_quat[1], -raw_quat[0], -raw_quat[2], raw_quat[3]]
                
            else:
                # Y-Up to Z-Up logic
                corrected_y = raw_pos[1] + height_offset + 0.02
                ue_pos = [raw_pos[0], -raw_pos[2], corrected_y]
                ue_quat = [raw_quat[0], -raw_quat[2], raw_quat[1], raw_quat[3]]
            
            bone_transforms[ue_name] = {
                "location": [p * 100 for p in ue_pos], # m to cm
                "rotation": ue_quat
            }
            
        # --ROOT MOTION ---
        #we create a fake "root" bone transform.
        # It takes the X and Y movement from the pelvis, but stays flat on the floor (Z = 0).
        #using the Identity quaternion [0,0,0,1] so it doesn't rotate, only translates.
        pelvis_pos_ue = bone_transforms[BONE_MAP[0]]["location"] # SMPL 0 is Pelvis
        bone_transforms["root"] = {
            "location": [pelvis_pos_ue[0], pelvis_pos_ue[1], 0.0], # X, Y, 0
            "rotation":[0.0, 0.0, 0.0, 1.0] # No rotation
        }
        
        output_frames.append({
            "frame_number": frame_idx,
            "bone_transforms": bone_transforms
        })

    #frame loop ends here.
    
    #final output dictionary
    final_output = {
        "meta": {        
            "frame_rate": smpl_output_data['frame_rate'],
            "total_frames": num_frames,
            "calibration_pose": calibration_pose
        },
        
        "frames": output_frames #a list of dictionaries, each representing a frame.
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
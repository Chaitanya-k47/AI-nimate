#This file contains the main logic and imports the BONE_MAP from the other file.
#run this command in terminal everytime you open this project: .\venv\Scripts\activate

import os
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
    
    #smpl_output_data is a dictionary of raw SMPL parameters from ML model.
    #it contains the keys: 'poses', 'betas', 'trans', 'frame_rate', etc.
    #extract this data into arrays and then convert it into tensors, for pytorch to process.
    poses_np = np.array(smpl_output_data['poses'])
    betas_np = np.array(smpl_output_data['betas'])
    trans_np = np.array(smpl_output_data['trans'])
    
    #poses_np shape: [num_frames, 72] (column index 0-2: root bone pose, column index 3-71: body pose), for each row(frame).
    num_frames = poses_np.shape[0] 
    #betas_np shape: [10,] (only first 10 are used for SMPL, rest are ignored) 1D array
    #trans_np shape: [num_frames, 3]

    #converting numpy arrays to torch tensors
    global_orient = torch.tensor(poses_np[:, :3], dtype=torch.float32).to(DEVICE) #poses for root bone.
    body_pose = torch.tensor(poses_np[:, 3:], dtype=torch.float32).to(DEVICE)
    betas = torch.tensor(betas_np, dtype=torch.float32).unsqueeze(0).to(DEVICE)
    
    #transl = torch.tensor(trans_np, dtype=torch.float32).to(DEVICE)
    #transl: [num_frames, 3]
    
    #betas require a batch dimension, to make it a 2D tensor.
    #created tensors are 2D and have shapes:
    #global_orient: [num_frames, 3]
    #body_pose: [num_frames, 69]
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
    joints_world = model_output.joints.squeeze(0).cpu().numpy()
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
    
    #reshape poses_np for easier processing
    #full pose is the global orientation + body pose
    full_pose_np = poses_np.reshape(num_frames, -1, 3)
    #full_pose_np shape: [num_frames, num_joints=24, 3]
    # -1 here means infer from other dimensions.
    
    #loop through frames and bones to create the final JSON structure.
    output_frames = [] #list(ordered) of frames. each frame is a dictionary of bone transforms.
    for frame_idx in range(num_frames):
        
        bone_transforms_for_this_frame = {} #dictionary of bone transforms for this frame.
        for smpl_bone_idx, ue_bone_name in BONE_MAP.items():
            
            #ROTATION(direct conversion, as we already have local rotation in axis-angle format)
            axis_angle = full_pose_np[frame_idx, smpl_bone_idx]
            r = R.from_rotvec(axis_angle)
            quat = r.as_quat() #[x, y, z, w]
            
            #TRANSLATION(local translation calculation relative to parent, from world positions)
            parent_idx = parents[smpl_bone_idx]
            if parent_idx == -1: #this is the root bone(pelvis)
                local_translation = joints_world[frame_idx, smpl_bone_idx]
            else:
                child_pos = joints_world[frame_idx, smpl_bone_idx]
                parent_pos = joints_world[frame_idx, parent_idx]
                local_translation = child_pos - parent_pos
            
            
            #coordinate system conversion from SMPL to UE
            #SMPL uses a right-handed coordinate system with Y-up.
            #Unreal Engine uses a left-handed coordinate system with Z-up.
            #UE_X = SMPL_X
            #UE_Y = -SMPL_Z
            #UE_Z = SMPL_Y

            #location:
            ue_translation = [
                local_translation[0],
                -local_translation[2],
                local_translation[1],
            ]
            
            #rotation:
            ue_quat = [
                quat[0], #x
                -quat[2], #y
                quat[1], #z
                quat[3] #w
            ]
            
            #add this bone's transform to the dictionary for this frame.           
            bone_transforms_for_this_frame[ue_bone_name] = {
                "location": [v*100 for v in ue_translation], #convert from meters to centimeters.
                "rotation": ue_quat
            }
        #bone loop ends here.
        
        # Handle Root Motion separately
        # This is the global movement of the entire character
        root_translation_vector = trans_np[frame_idx]
        ue_root_translation = [
            root_translation_vector[0],
            -root_translation_vector[2],
            root_translation_vector[1],
        ]
        
        output_frames.append(
            {
                "frame_number": frame_idx,
                "bone_transforms": bone_transforms_for_this_frame,
                "root_transform":{
                    "location": [v*100 for v in ue_root_translation], #convert from meters to centimeters.
                }
            }
        )
    #frame loop ends here.
    
    #final output dictionary
    final_output = {
        "meta": {        
            "frame_rate": smpl_output_data['frame_rate'],
            "total_frames": num_frames
        },
        
        "frames": output_frames #a list of dictionaries, each representing a frame.
    }
    
    return final_output
    #return {} #temporary return to avoid errors while debugging.


#----------------TESTING BLOCK------------------
# This part of the script will only run when you execute `python smpl_converter.py`
# directly. It allows us to test the converter without a full server or ML model.
if __name__ == '__main__':
    
    def generate_dummy_data(num_frames=60):
        """Creates a sample input dictionary with random motion."""
        print(f"Generating dummy SMPL data for {num_frames} frames...")
        # A real model would output this. Here, we create random noise for testing.
        # Shape of poses: (num_frames, 72)
        # Shape of trans: (num_frames, 3)
        # Shape of betas: (16,) -> smplx requires 16, but only uses first 10 for SMPL
        dummy_data = {
            'poses': np.random.rand(num_frames, 72) * 0.1, # Small random rotations
            'trans': np.random.rand(num_frames, 3) * 0.01, # Small random movements
            'betas': np.zeros(10),
            'frame_rate': 30
        }
        # Set a more interesting root motion (e.g., walking forward)
        for i in range(num_frames):
             dummy_data['trans'][i, 0] = i * 0.02 # Move forward on X axis
        
        return dummy_data


    # 1. Generate some fake data that looks like our ML model's output
    test_smpl_data = generate_dummy_data(num_frames=90)

    # 2. Run our conversion function
    print("Running SMPL to UE conversion...")
    ue_json_output = convert_smpl_to_ue_json(test_smpl_data)
    print("Conversion complete.")

    script_dir = os.path.dirname(__file__)
    relative_target_dir = os.path.join('..', 'unreal', 'AInimate')
    target_dir = os.path.join(script_dir, relative_target_dir)
    os.makedirs(target_dir, exist_ok=True)
    output_filepath = os.path.join(target_dir, "test_output.json")
    
    with open(output_filepath, 'w') as f:
        json.dump(ue_json_output, f, indent=4)
    
    print(f"\nSuccessfully generated animation data.")
    print(f"Output saved to: {output_filepath}")
    print(f"Total frames: {ue_json_output['meta']['total_frames']}")
       

            
    
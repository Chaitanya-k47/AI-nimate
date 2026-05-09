import os
import shutil
import tkinter as tk
from tkinter import filedialog, messagebox
import torch
import smplx
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# --- CONFIGURATION ---
MODEL_PATH = './smpl_models'
NPZ_PATH = 'input.npz'
GENDER = 'neutral'
DEVICE = torch.device('cpu')

def load_and_solve_smpl(npz_path):
    print(f"Loading SMPL model from {MODEL_PATH}...")
    smpl_model = smplx.create(
        model_path=MODEL_PATH,
        model_type='smpl',
        gender=GENDER,
        use_pca=False,
        flat_hand_mean=True
    ).to(DEVICE)

    print(f"Loading NPZ data from {npz_path}...")
    data = np.load(npz_path)
    
    # Robust extraction
    if 'poses' in data.files:
        full_poses = data['poses']
        if full_poses.shape[1] > 72:
            full_poses = full_poses[:, :72]
    elif 'global_orient' in data.files and 'body_pose' in data.files:
        global_orient = data['global_orient']
        body_pose = data['body_pose']
        full_poses = np.concatenate((global_orient, body_pose), axis=1)
    else:
        raise KeyError("Could not find 'poses' or 'global_orient' in NPZ.")
    
    current_cols = full_poses.shape[1]
    if current_cols < 72:
        padding = np.zeros((full_poses.shape[0], 72 - current_cols))
        full_poses = np.concatenate((full_poses, padding), axis=1)

    trans = data['transl'] if 'transl' in data.files else data.get('trans', np.zeros((full_poses.shape[0], 3)))
    betas = data['betas'] if 'betas' in data.files else np.zeros(10)
    if betas.ndim == 2: betas = betas[0]
    if betas.shape[0] > 10: betas = betas[:10]

    num_frames = full_poses.shape[0]

    # Convert to tensors
    global_orient_tensor = torch.tensor(full_poses[:, :3], dtype=torch.float32).to(DEVICE)
    body_pose_tensor = torch.tensor(full_poses[:, 3:], dtype=torch.float32).to(DEVICE)
    betas_tensor = torch.tensor(betas, dtype=torch.float32).unsqueeze(0).to(DEVICE)

    print("Running Forward Kinematics...")
    with torch.no_grad():
        model_output = smpl_model(
            betas=betas_tensor,
            body_pose=body_pose_tensor,
            global_orient=global_orient_tensor,
            return_verts=False
        )

    # Get absolute 3D joint positions:[Frames, 24 joints, 3 coordinates]
    joints_3d = model_output.joints.squeeze(0).cpu().numpy()[:, :24, :] + trans[:, np.newaxis, :]
    parents = smpl_model.parents.cpu().numpy()[:24]

    return joints_3d, parents, num_frames

def set_axes_equal(ax):
    """Make axes of 3D plot have equal scale so that spheres appear as spheres."""
    x_limits = ax.get_xlim3d()
    y_limits = ax.get_ylim3d()
    z_limits = ax.get_zlim3d()

    x_range = abs(x_limits[1] - x_limits[0])
    x_middle = np.mean(x_limits)
    y_range = abs(y_limits[1] - y_limits[0])
    y_middle = np.mean(y_limits)
    z_range = abs(z_limits[1] - z_limits[0])
    z_middle = np.mean(z_limits)

    plot_radius = 0.5 * max([x_range, y_range, z_range])

    ax.set_xlim3d([x_middle - plot_radius, x_middle + plot_radius])
    ax.set_ylim3d([y_middle - plot_radius, y_middle + plot_radius])
    ax.set_zlim3d([z_middle - plot_radius, z_middle + plot_radius])

def visualize():
    joints_3d, parents, num_frames = load_and_solve_smpl(NPZ_PATH)

    print(f"Preparing visualization for {num_frames} frames...")

    fig = plt.figure()
    ax = fig.add_subplot(111, projection='3d')
    
    # Create bone pairs from the parent array
    bones = [(i, parents[i]) for i in range(1, 24)]

    # Initialize empty lines for the bones
    lines =[ax.plot([], [],[], c='blue', linewidth=2)[0] for _ in bones]
    scatter = ax.scatter([],[],[], c='red', s=10)

    # --- CHANGED: RAW SMPL COORDINATE MAPPING ---
    # We no longer swap axes. We pipe SMPL directly into Matplotlib.
    all_x = joints_3d[:, :, 0] # SMPL X
    all_y = joints_3d[:, :, 1] # SMPL Y (This is "Up" in SMPL)
    all_z = joints_3d[:, :, 2] # SMPL Z (This is "Depth" in SMPL)

    ax.set_xlim3d(np.min(all_x), np.max(all_x))
    ax.set_ylim3d(np.min(all_y), np.max(all_y))
    ax.set_zlim3d(np.min(all_z), np.max(all_z))
    set_axes_equal(ax)

    # Label the axes to clearly show what data is driving them
    ax.set_xlabel('Raw SMPL X')
    ax.set_ylabel('Raw SMPL Y (SMPL UP)')
    ax.set_zlabel('Raw SMPL Z (SMPL Depth)')
    
    # Since Matplotlib's Z is physically "Up" on the screen, the character will lie down.
    ax.set_title("Raw SMPL Data (No Coordinate Swizzling)")

    def update(frame):
        current_joints = joints_3d[frame]
        
        # --- CHANGED: RAW SMPL COORDINATE UPDATE ---
        for i, (child, parent) in enumerate(bones):
            x_data = [current_joints[child, 0], current_joints[parent, 0]]
            y_data = [current_joints[child, 1], current_joints[parent, 1]] # Pass Y straight to Y
            z_data = [current_joints[child, 2], current_joints[parent, 2]] # Pass Z straight to Z
            
            lines[i].set_data(x_data, y_data)
            lines[i].set_3d_properties(z_data)
            
        scatter._offsets3d = (current_joints[:, 0], current_joints[:, 1], current_joints[:, 2])
        return lines + [scatter]

    # Create the animation
    anim = animation.FuncAnimation(
        fig, update, frames=num_frames, interval=1000/30, blit=False
    )

    print("Playing animation! Close the window to exit.")
    plt.show()

# --- GUI BROWSER AND FILE COPY LOGIC ---
def browse_and_copy_npz():
    root = tk.Tk()
    root.withdraw()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    accad_dir = os.path.join(script_dir, "ACCAD")
    target_filepath = os.path.join(script_dir, NPZ_PATH) 

    if not os.path.exists(accad_dir):
        messagebox.showerror("Folder Not Found", f"Could not find the ACCAD folder at:\n{accad_dir}\n\nPlease ensure it is placed in the same directory as this script.")
        return False

    file_path = filedialog.askopenfilename(
        initialdir=accad_dir,
        title="Select an AMASS/ACCAD NPZ Animation File",
        filetypes=(("NPZ Animation Data", "*.npz"), ("All files", "*.*"))
    )

    if file_path:
        print(f"Selected file: {file_path}")
        try:
            shutil.copy2(file_path, target_filepath)
            print(f"Successfully copied and renamed to: {target_filepath}")
            return True
        except Exception as e:
            messagebox.showerror("Copy Error", f"Failed to copy the file:\n{e}")
            return False
    else:
        print("File selection cancelled by user.")
        return False

# --- MAIN EXECUTION ---
if __name__ == '__main__':
    if browse_and_copy_npz():
        visualize()
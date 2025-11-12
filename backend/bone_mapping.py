# bone_mapping.py

"""
This file contains the mapping from the SMPL model's joint indices to the
bone names of the Unreal Engine 5 Mannequin skeleton (SK_Mannequin).

The SMPL model has 24 joints in its standard configuration. We are mapping
the most important of these to their corresponding bones in the UE5 skeleton.
This map is crucial for the smpl_converter.py script to correctly format
the animation data for Unreal Engine.
"""

# MAPPING: SMPL Joint Index -> Unreal Engine Bone Name
BONE_MAP = {
    # -- Root and Spine --
    0:  'pelvis',       # SMPL's root joint is the Pelvis/Hips.
    3:  'spine_01',
    6:  'spine_02',
    9:  'spine_03',     # The UE5 skeleton has a highly detailed spine.
    
    # -- Head and Neck --
    12: 'neck_01',
    15: 'head',

    # -- Left Arm --
    13: 'clavicle_l',   # Left Collarbone
    16: 'upperarm_l',
    18: 'lowerarm_l',
    20: 'hand_l',

    # -- Right Arm --
    14: 'clavicle_r',   # Right Collarbone
    17: 'upperarm_r',
    19: 'lowerarm_r',
    21: 'hand_r',

    # -- Left Leg --
    1:  'thigh_l',
    4:  'calf_l',
    7:  'foot_l',
    10: 'ball_l',       # Corresponds to the ball of the foot for toe rotation.

    # -- Right Leg --
    2:  'thigh_r',
    5:  'calf_r',
    8:  'foot_r',
    11: 'ball_r',       # Corresponds to the ball of the foot for toe rotation.
}

# Note: Some SMPL joints are intentionally omitted as they don't have a
# direct, one-to-one equivalent in the standard game-ready UE5 skeleton's
# animation bones. For example:
# - SMPL joints 22, 23 are often related to toes, which are simplified in UE.
# - The UE5 skeleton has additional "twist" bones (e.g., lowerarm_twist_01_l)
#   and IK bones (e.g., ik_foot_root) which are not part of the primary SMPL
#   animation data and are typically driven by engine-side constraints.
# This map focuses on the core deformer bones that directly receive mocap data.
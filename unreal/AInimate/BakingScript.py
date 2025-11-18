import unreal
import sys

#configuration
LEVEL_SEQUENCE_PATH = "/Game/AInimate/AInimateRuntimeSequence.AInimateRuntimeSequence"
OUTPUT_PATH = "/Game/AInimate/Animations/"
TARGET_SKELETON_PATH = "PLACEHOLDER_SKELETON_PATH" 

#main logic
try:
    unreal.log("Python baking script started...")

    #1. load assets and subsystems
    level_sequence = unreal.load_asset(LEVEL_SEQUENCE_PATH)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor_subsystem.get_editor_world()
    
    if not level_sequence:
        raise Exception(f"Failed to load Level Sequence: {LEVEL_SEQUENCE_PATH}")

    # 2. load the Skeleton asset
    unreal.log(f"Attempting to load skeleton at path: {TARGET_SKELETON_PATH}")
    target_skeleton = unreal.load_asset(TARGET_SKELETON_PATH)
    
    if not target_skeleton:
        raise Exception(f"Python could not load the skeleton asset at path: {TARGET_SKELETON_PATH}.")

    # 3. get the binding
    bindings = level_sequence.get_bindings()
    if not bindings:
        raise Exception("Level Sequence has no bindings. The C++ step may have failed.")
    character_binding = bindings[0]

    # 4. create a unique asset name
    import time
    timestamp = int(time.time())
    asset_name = f"GeneratedAnim_{timestamp}"

    # 5. create the animation sequence asset
    factory = unreal.AnimSequenceFactory()
    factory.set_editor_property("target_skeleton", target_skeleton)

    new_anim_sequence = asset_tools.create_asset(
        asset_name=asset_name,
        package_path=OUTPUT_PATH,
        asset_class=unreal.AnimSequence,
        factory=factory
    )   

    if not new_anim_sequence:
        raise Exception("Failed to create a new, empty AnimSequence asset.")

    # 6. set up export options
    export_options = unreal.AnimSeqExportOption()
    export_options.export_transforms = True

    # 7. call export_anim_sequence
    unreal.log("Executing export_anim_sequence...")
    unreal.SequencerTools.export_anim_sequence(
        world,
        level_sequence,
        new_anim_sequence,
        export_options,
        character_binding,
        False
    )

    # 8. save the populated asset
    asset_path = new_anim_sequence.get_path_name()
    unreal.log(f"Saving populated asset at: {asset_path}")
    if unreal.EditorAssetLibrary.save_asset(asset_path):
        unreal.log(f"SUCCESS: Animation baked and saved to: {asset_path}")
    else:
        unreal.log_warning(f"Baking succeeded, but the final asset could not be saved at: {asset_path}")
        raise Exception("Failed to save the final asset.")

except Exception as e:
    unreal.log_error(f"FATAL: Python baking script failed. Reason: {e}")
    sys.exit(1)
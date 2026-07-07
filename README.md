# NTE Remove Blur Tool

Unreal Engine 5.6 editor plugin for generating Animation Blueprint nodes that reduce or remove the near-camera dither fade used by some NTE character materials.

## What It Generates

For each selected material slot, the plugin creates a Dynamic Material Instance and writes a default anti-fade scalar preset:

- `FPSCameFade = 0`
- `FPSCameraCutHeight = -99999`
- `FPSCameraCutHeightOverride = -99999`
- `PlayerDitherRandom = 0`
- `OpacityMaskVal = 1`
- `MaskOpacity = 1`
- `InvisableSkillOpacity = 1`
- `PlayerInvisable = 1`
- `Opacity = 1`
- `Dissolve = 0`
- `DissolveSkill = 0`

## Usage

1. Enable the plugin in the UE editor.
2. Open `Tools > NTE Remove Blur Tool`.
3. Choose the character Animation Blueprint.
4. Enter material slot IDs, for example `3` or `1,5,6`.
5. Click `Generate Remove Blur Nodes`.

The tool writes nodes into `BlueprintInitializeAnimation` and `BlueprintUpdateAnimation`, then optionally saves the Animation Blueprint.

## Notes

This works when the material exposes the fade controls as scalar parameters. If the fade is hard-coded in the parent material graph and ignores these parameters, the parent material must be edited or replaced.

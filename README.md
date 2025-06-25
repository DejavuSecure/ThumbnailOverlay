# ThumbnailOverlay

An example of using `DwmRegisterThumbnail` to completely replicate content from another window and overlay custom content on top of it.

## How to Use

1. **Change the target window:**
   ```cpp
   CloneWindow cloneWindow((HWND)FindWindowA("UnrealWindow", NULL));
   ```
   Replace `"UnrealWindow"` with your target window class name.

2. **Implement your custom drawing:**
   Write your drawing code in the `CustomDraw` function:
   ```cpp
   void CustomDraw(OverlayWindow* Overlay, int Width, int Height)
   ```

## Usage Instructions

- Modify the window class name in step 1 to match the window you want to clone
- Implement your overlay graphics in the `CustomDraw` function where you have access to the overlay window object and dimensions

![QQ_1750832483851](https://github.com/user-attachments/assets/541b2270-6130-4243-a047-c54a5d860a0e)


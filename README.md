# upscale-injection

## Build and run
- Open upscale-injection/upscale-injection.sln and build the entire
solution.
- Test executable (d3d11_test_app.exe) and the DLL with the hook
(d3d11.dll) will be placed in the folder build/your_configuration, and
shader folders content and hook_content will be copied there.
- After running the test executable you can see zoomed interpolated frames
of the original spinning cube from the test app, and the FPS for each frame
- You can find screenshots and demo video showing the whole process in
screenshots folder.
- The solution is made with Microsoft Visual Studio Community 2019,
version 16.11.29 and platform toolset v142.
- To test the DLL with another game put d3d11.dll and hook_content folder
alongside the main executable of the game. The DLL will read the shaders
from the hook_content folder and preform upscaling (file names must be
preserved)
- Please be aware what due to “hacker’s” nature of the injection, Microsoft
Defender or other antivirus software may block execution of the hook.
Consider disabling antivirus software before using the DLL.
- If the test app asks for vcruntime140 dlls, you can find them at the bin
folder. It’s also noted that the test app from the original author of the
injection may have issues running from some system folders, i.e Users or
Downloads. In that case putting it in a path like C:\\upscale-injection
should solve the issue.

Video demo:

[![demo](https://img.youtube.com/vi/LsQVTyMNhPA/0.jpg)](https://www.youtube.com/watch?v=LsQVTyMNhPA)


### Some notes on the implementation 
- upscaling, cropping to the top-left quadrant and displaying of the FPS is
done in one compute shader named upscale_cs.shader located in the
folder upscale-injection\d3d11_proxy_dll\hook_content
- instead of upscaling the whole image and then crop the top-left quadrant,
the shader upscales just the top-left quadrant to save computation for the
parts of the image we don’t display anyway 
- compute shader is chosen for better performance and also can be used for
further extension to more advanced upscaling approaches, like super
resolution CNN and GAN shader implementations

@echo off
if not defined DevEnvDir (
	call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
)
cl /EHsc /nologo /DNDEBUG /std:c++17 App.cpp  /O2 .\Ext\nanogui.lib .\Ext\glfw3.lib user32.lib /I.\Ext\nanogui\include -DNANOGUI_USE_OPENGL /I.\Ext\ /I.\Ext\nanogui\ext\nanovg\src
cl /EHsc /nologo /std:c++17 UnitTests.cpp  .\Ext\nanogui.lib .\Ext\glfw3.lib user32.lib /I.\Ext\nanogui\include -DNANOGUI_USE_OPENGL /I.\Ext\ /I.\Ext\nanogui\ext\nanovg\src
# Polling Bridge Linking Issue Troubleshooting

## Problem
ESPHome build fails with: `undefined reference to polling_bridge_init`

## Root Cause Analysis
The linkage is correctly configured (extern "C" properly used, tests pass). The issue is that `polling_bridge.cpp` is not being compiled by ESPHome/PlatformIO.

## Verification Steps

### 1. Check if file is being compiled
Look for this in the build output:
```
Compiling .pioenvs/zoneline-esphome/src/esphome/components/geappliances_bridge/polling_bridge.cpp.o
```

If this line is **missing**, the file isn't being compiled.

### 2. Verify file exists in build directory
After ESPHome generates code, check:
```bash
ls -la /data/build/zoneline-esphome/src/esphome/components/geappliances_bridge/
```

Should contain `polling_bridge.cpp` and `polling_bridge.h`.

## Solutions

### Solution 1: Clear Caches (Try First)
```bash
# Clear ESPHome cache
rm -rf ~/.esphome/
rm -rf /config/.esphome/

# Clear PlatformIO cache  
cd /config/esphome
pio run --target clean

# Rebuild
esphome run zoneline-esphome.yaml
```

### Solution 2: Force Component Refresh
In your YAML, modify the external_components section to force re-download:
```yaml
external_components:
  - source: github://joshualongenecker/home-assistant-bridge-esphome@copilot/add-erd-selection-functionality
    refresh: always  # Add this line
    components: [ geappliances_bridge ]
```

### Solution 3: Check PlatformIO Build Filters
The component might have build filters excluding the file. Check `.pio/build/*/platformio.ini` for any `build_src_filter` settings that might exclude `polling_bridge.cpp`.

### Solution 4: Verify Component Structure
Ensure the component directory structure is:
```
components/geappliances_bridge/
├── __init__.py
├── polling_bridge.cpp
├── polling_bridge.h  
├── mqtt_bridge.cpp
├── mqtt_bridge.h
└── (other files)
```

## Expected Build Output (Success)
When working correctly, you should see:
```
INFO Compiling app...
Compiling .pioenvs/.../geappliances_bridge.cpp.o
Compiling .pioenvs/.../polling_bridge.cpp.o  <-- THIS LINE IS CRITICAL
Compiling .pioenvs/.../mqtt_bridge.cpp.o
...
Linking .pioenvs/.../firmware.elf
```

## If Still Failing
1. Check ESPHome version: `esphome version`
2. Try with a local component (copy to `config/esphome/components/`)
3. Share full build log showing the compilation phase

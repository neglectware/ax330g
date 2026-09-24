# Factory presets

Each folder here is a read-only folder in the plugin's preset browser, shown with a lock: `presets/factory/<Folder name>/<Preset name>.ax330g`.
One level of folders only; a file directly in this folder shows under "Factory".
The files are the plugin's own preset format (`plugin-chain/src/presets/Presets.h`), so a preset saved in the plugin can be copied here as it is.
CMake bundles every `.ax330g` file into the plugin when it configures; add, change or remove a file and rebuild.

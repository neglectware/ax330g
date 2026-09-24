# Factory presets

Each folder here is a read-only bank in the plugin's preset browser, shown with a lock: `presets/factory/<Folder name>/<Preset name>.ax330g`.
One level of folders only; a file directly in this folder shows under "Factory".
The files are the plugin's own preset format (`plugin-chain/src/presets/Presets.h`), so a preset saved in the plugin can be copied here as it is.
Give every preset a `"number"` from 1 to 999, unique in its folder.

Bank letters (0.11.0): put a `bank.json` in each folder, for example `{"letter": "A"}`. A factory folder without one gets the first free letter when the plugin reads it (in memory only). A factory letter always wins over a user bank with the same letter.

CMake bundles every `.ax330g` file and every `bank.json` into the plugin when it configures; add, change or remove a file and rebuild.

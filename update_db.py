import sys
Import("env")

# Prevent the infinite loop by checking if we are already generating the database
if "compiledb" not in sys.argv:
    # Scoped explicitly to the board env: `pio run -t compiledb` with no -e
    # processes every environment in platformio.ini declaration order and
    # *overwrites* compile_commands.json each pass instead of merging, so
    # whichever env is declared last always wins. That was silently
    # clobbering the real firmware include paths with env:native's
    # bare-host command lines (no Arduino/WiFiNINA/Protomatter headers),
    # breaking clangd diagnostics for everything under src/.
    env.Execute("pio run -t compiledb -e adafruit_matrix_portal_m4")
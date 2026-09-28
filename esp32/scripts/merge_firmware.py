Import("env")


def merge_factory_image(source, target, env):
    images = [(offset, env.subst(path)) for offset, path in env.get("FLASH_EXTRA_IMAGES", [])]
    images.append((env.subst("$ESP32_APP_OFFSET"), target[0].get_abspath()))
    parts = " ".join(f'{offset} "{path}"' for offset, path in images)
    mcu = env.BoardConfig().get("build.mcu")
    cmd = f'"$PYTHONEXE" "$OBJCOPY" --chip {mcu} merge_bin -o "$BUILD_DIR/${{PROGNAME}}.factory.bin" {parts}'
    if env.Execute(cmd) != 0:
        env.Exit(1)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_factory_image)

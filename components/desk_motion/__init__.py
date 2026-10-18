"""Package the standalone desk motion engine for ESPHome."""

import esphome.codegen as cg
import esphome.config_validation as cv

CODEOWNERS = ["@jasoncronje"]
DEPENDENCIES = ["esp32"]
CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    cg.add_global(
        cg.RawStatement('#include "esphome/components/desk_motion/desk_motion.h"')
    )

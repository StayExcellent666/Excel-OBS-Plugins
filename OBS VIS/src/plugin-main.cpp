#include <obs-module.h>

#include "visualizer-source.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-spectrum-canvas", "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
  return "ExcelVisus";
}

MODULE_EXPORT const char *obs_module_description(void)
{
  return "ExcelVisus real-time audio visualizations for OBS";
}

bool obs_module_load(void)
{
  register_visualizer_source();
  blog(LOG_INFO, "[ExcelVisus] loaded version 0.4.0");
  return true;
}

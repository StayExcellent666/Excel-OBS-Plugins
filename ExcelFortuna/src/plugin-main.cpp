#include <obs-module.h>

#include "fortuna-source.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-excelfortuna", "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
  return "ExcelFortuna";
}

MODULE_EXPORT const char *obs_module_description(void)
{
  return "ExcelFortuna Channel Points giveaway wheel for OBS";
}

bool obs_module_load(void)
{
  register_fortuna_source();
  blog(LOG_INFO, "[ExcelFortuna] loaded version 0.2.1");
  return true;
}

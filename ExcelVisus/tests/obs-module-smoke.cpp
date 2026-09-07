#include <obs.h>

#include <cstring>
#include <iostream>

int main(int argc, char **argv)
{
  if (argc != 3) {
    std::cerr << "usage: obs-module-smoke <plugin.dll> <plugin-data-dir>\n";
    return 2;
  }

  if (!obs_startup("en-US", nullptr, nullptr)) {
    std::cerr << "obs_startup failed\n";
    return 1;
  }

  obs_module_t *module = nullptr;
  const int open_result = obs_open_module(&module, argv[1], argv[2]);
  if (open_result != MODULE_SUCCESS) {
    std::cerr << "obs_open_module failed with code " << open_result << '\n';
    obs_shutdown();
    return 1;
  }
  if (!obs_init_module(module)) {
    std::cerr << "obs_init_module failed\n";
    obs_shutdown();
    return 1;
  }

  bool found = false;
  for (std::size_t index = 0;; ++index) {
    const char *id = nullptr;
    if (!obs_enum_input_types(index, &id))
      break;
    if (id && std::strcmp(id, "spectrum_canvas_source") == 0) {
      found = true;
      break;
    }
  }

  obs_shutdown();
  if (!found) {
    std::cerr << "module loaded but source type was not registered\n";
    return 1;
  }
  std::cout << "OBS module initialization and source registration passed\n";
  return 0;
}

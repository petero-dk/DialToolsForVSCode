{
  "targets": [
    {
      "target_name": "radial_controller",
      "sources": ["src/radial_controller.cpp"],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      "defines": [
        "NAPI_DISABLE_CPP_EXCEPTIONS",
        "_WIN32_WINNT=0x0A00",
        "NOMINMAX",
        "WIN32_LEAN_AND_MEAN"
      ],
      "conditions": [
        [
          "OS=='win'",
          {
            "msvs_settings": {
              "VCCLCompilerTool": {
                "AdditionalOptions": ["/std:c++17", "/EHsc", "/await:strict"],
                "ExceptionHandling": "1"
              }
            },
            "libraries": [
              "-lruntimeobject.lib",
              "-lole32.lib",
              "-luser32.lib"
            ]
          }
        ]
      ]
    },
    {
      "target_name": "radial_hook",
      "type": "shared_library",
      "sources": ["src/radial_hook.cpp"],
      "defines": [
        "_WIN32_WINNT=0x0A00",
        "NOMINMAX",
        "WIN32_LEAN_AND_MEAN",
        "RADIAL_HOOK_EXPORTS"
      ],
      "conditions": [
        [
          "OS=='win'",
          {
            "msvs_settings": {
              "VCCLCompilerTool": {
                "AdditionalOptions": ["/std:c++17", "/EHsc", "/await:strict"],
                "ExceptionHandling": "1"
              }
            },
            "libraries": [
              "-lruntimeobject.lib",
              "-lole32.lib",
              "-luser32.lib"
            ]
          }
        ]
      ]
    }
  ]
}

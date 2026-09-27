thifo_add_plugin(thfGrain
    PRODUCT_NAME "thf Grain"
    PLUGIN_CODE  Gr01
    BUNDLE_ID    com.thf.thfgrain
    IS_SYNTH     TRUE
    SOURCES
        Source/Parameters.cpp
        Source/Presets.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/dsp/GrainEngine.cpp
        Source/dsp/SourceData.cpp
        Source/ui/GrainViews.cpp
        Source/ui/WaveformView.cpp
    RESOURCES
        Resources/ru_grain.txt)

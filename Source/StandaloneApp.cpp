// Standalone application for WJ-8000.
//
// Same as JUCE's default standalone app, except that the window uses the operating
// system's own title bar and the Options button sits in the top-right corner of the window.
// Only compiled into the Standalone target (see CMakeLists.txt).

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
class WJStandaloneWindow final : public juce::StandaloneFilterWindow
{
public:
    using juce::StandaloneFilterWindow::StandaloneFilterWindow;

    void init()
    {
        setUsingNativeTitleBar(true);
        findOptionsButton();
        resized();
    }

    void resized() override
    {
        juce::StandaloneFilterWindow::resized();
        // The base class puts the button in the (JUCE-drawn) title bar; with the native
        // title bar there is none, so move it to the top-right corner of the window.
        if (optionsButton != nullptr)
        {
            optionsButton->setBounds(getWidth() - 68, 3, 60, 22);
            optionsButton->toFront(false);
        }
    }

private:
    void findOptionsButton()
    {
        for (auto* c : getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*>(c))
                if (b->getButtonText() == "Options")
                    optionsButton = b;
    }

    juce::TextButton* optionsButton = nullptr;
};

class WJStandaloneApp final : public juce::JUCEApplication
{
public:
    WJStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = juce::CharPointer_UTF8(JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif
        appProperties.setStorageParameters(options);
    }

    const juce::String getApplicationName() override             { return juce::CharPointer_UTF8(JucePlugin_Name); }
    const juce::String getApplicationVersion() override          { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override                   { return true; }
    void anotherInstanceStarted(const juce::String&) override    {}

    void initialise(const juce::String&) override
    {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            quit();
            return;
        }

        auto holder = std::make_unique<juce::StandalonePluginHolder>(appProperties.getUserSettings(),
                                                                     false, juce::String{}, nullptr,
                                                                     juce::Array<juce::StandalonePluginHolder::PluginInOuts>{},
                                                                     false);
        mainWindow = std::make_unique<WJStandaloneWindow>(
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
            std::move(holder));
        mainWindow->init();
        mainWindow->setVisible(true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay(100, []()
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<WJStandaloneWindow> mainWindow;
};
} // namespace

JUCE_CREATE_APPLICATION_DEFINE(WJStandaloneApp)

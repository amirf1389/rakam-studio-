#include "UI/BrowserPanel.h"

using namespace Rakam::Colours;

BrowserPanel::BrowserPanel (AudioEngine& e) : engine (e)
{
    for (auto* b : { &filesTab, &effectsTab })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->setColour (juce::TextButton::buttonOnColourId, panelLight.brighter (0.1f));
        b->setColour (juce::TextButton::textColourOnId, accent);
        addAndMakeVisible (b);
    }
    filesTab.onClick   = [this] { showTab (0); };
    effectsTab.onClick = [this] { showTab (1); };

    // Locations
    auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    auto docs  = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    auto desk  = juce::File::getSpecialLocation (juce::File::userDesktopDirectory);
    auto home  = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    locations = { Rakam::getUserFolder(), music, home.getChildFile ("Downloads"), desk, docs };
    const juce::StringArray names { "Rakam Studio", "Music", "Downloads", "Desktop", "Documents" };

    for (int i = 0; i < locations.size(); ++i)
        locationBox.addItem (names[i], i + 1);
    locationBox.addSeparator();
    locationBox.addItem ("Choose folder...", 100);

    const auto saved = juce::File (engine.getSettings().getValue ("browserRoot"));
    locationBox.onChange = [this]
    {
        const int id = locationBox.getSelectedId();
        if (id == 100)
        {
            chooser = std::make_unique<juce::FileChooser> ("Choose a sample folder", contents.getDirectory());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult().isDirectory())
                                          setRoot (fc.getResult());
                                  });
            locationBox.setText (contents.getDirectory().getFileName(), juce::dontSendNotification);
        }
        else if (id > 0)
        {
            setRoot (locations[id - 1]);
        }
    };
    addAndMakeVisible (locationBox);

    previewButton.setClickingTogglesState (true);
    previewButton.setToggleState (engine.getSettings().getBoolValue ("browserPreview", true), juce::dontSendNotification);
    previewButton.setTooltip ("Play files when you click them");
    previewButton.onClick = [this]
    {
        engine.getSettings().setValue ("browserPreview", previewButton.getToggleState());
        if (! previewButton.getToggleState())
            engine.stopPreview();
    };
    addAndMakeVisible (previewButton);

    directoryThread.startThread (juce::Thread::Priority::low);
    fileTree.addListener (this);
    fileTree.setDragAndDropDescription (dragDescription);
    fileTree.setColour (juce::TreeView::backgroundColourId, background);
    fileTree.setColour (juce::FileTreeComponent::highlightColourId, accent.withAlpha (0.35f));
    fileTree.setColour (juce::FileTreeComponent::textColourId, text);
    fileTree.setColour (juce::TreeView::linesColourId, gridStrong);
    addAndMakeVisible (fileTree);

    effectList.setRowHeight (24);
    effectList.setColour (juce::ListBox::backgroundColourId, background);
    addChildComponent (effectList);

    hint.setColour (juce::Label::textColourId, textDim);
    hint.setFont (juce::FontOptions (11.5f));
    hint.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (hint);

    setRoot (saved.isDirectory() ? saved : Rakam::getUserFolder());
    refreshEffects();
    engine.getKnownPlugins().addChangeListener (this);
    showTab (0);
}

BrowserPanel::~BrowserPanel()
{
    engine.getKnownPlugins().removeChangeListener (this);
    fileTree.removeListener (this);
    directoryThread.stopThread (2000);
}

void BrowserPanel::setRoot (const juce::File& folder)
{
    folder.createDirectory();
    contents.setDirectory (folder, true, true);
    engine.getSettings().setValue ("browserRoot", folder.getFullPathName());

    const int index = locations.indexOf (folder);
    if (index >= 0)
        locationBox.setSelectedId (index + 1, juce::dontSendNotification);
    else
        locationBox.setText (folder.getFileName(), juce::dontSendNotification);
}

void BrowserPanel::showTab (int index)
{
    currentTab = index;
    filesTab.setToggleState (index == 0, juce::dontSendNotification);
    effectsTab.setToggleState (index == 1, juce::dontSendNotification);

    fileTree.setVisible (index == 0);
    locationBox.setVisible (index == 0);
    previewButton.setVisible (index == 0);
    effectList.setVisible (index == 1);

    hint.setText (index == 0 ? "Click to preview - drag or double-click to import"
                             : "Double-click to add to the selected track",
                  juce::dontSendNotification);
    resized();
}

void BrowserPanel::refreshEffects()
{
    effects.clear();
    effects.push_back ({ "BUILT-IN", {} });
    for (auto& d : BuiltInPluginFormat::getAllDescriptions())
        effects.push_back ({ {}, d });

    auto types = engine.getKnownPlugins().getTypes();
    std::sort (types.begin(), types.end(), [] (auto& a, auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });

    bool header = false;
    for (auto& d : types)
    {
        if (d.isInstrument)
            continue;
        if (! header)
        {
            effects.push_back ({ "VST3", {} });
            header = true;
        }
        effects.push_back ({ {}, d });
    }

    if (! header)
        effects.push_back ({ "VST3: Options > Plugin Manager to scan", {} });

    effectList.updateContent();
    effectList.repaint();
}

void BrowserPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) effects.size()))
        return;

    auto& item = effects[(size_t) row];

    if (item.header.isNotEmpty())
    {
        g.setColour (textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (item.header, 10, 0, width - 12, height, juce::Justification::bottomLeft);
        return;
    }

    if (selected)
    {
        g.setColour (accent.withAlpha (0.3f));
        g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, 1.0f, (float) width - 8.0f, (float) height - 2.0f), 4.0f);
    }

    const bool builtIn = item.desc.pluginFormatName == BuiltInPluginFormat::formatName;
    g.setColour (builtIn ? accent : monitor);
    g.fillEllipse (14.0f, (float) height * 0.5f - 3.0f, 6.0f, 6.0f);

    g.setColour (text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (item.desc.name, 28, 0, width - 34, height, juce::Justification::centredLeft, true);

    if (! builtIn)
    {
        g.setColour (textDim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (item.desc.manufacturerName, 28, 0, width - 34, height, juce::Justification::centredRight, true);
    }
}

void BrowserPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (row, (int) effects.size()) && effects[(size_t) row].header.isEmpty() && onAddEffect)
        onAddEffect (effects[(size_t) row].desc);
}

void BrowserPanel::selectionChanged()
{
    const auto file = fileTree.getSelectedFile();
    if (previewButton.getToggleState() && file.existsAsFile())
        engine.previewFile (file);
}

void BrowserPanel::fileDoubleClicked (const juce::File& file)
{
    if (file.existsAsFile())
    {
        engine.stopPreview();
        if (onImportFile)
            onImportFile (file);
    }
}

void BrowserPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (grid);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void BrowserPanel::resized()
{
    auto r = getLocalBounds().withTrimmedRight (1).reduced (8);

    auto tabs = r.removeFromTop (26);
    filesTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2).withTrimmedRight (2));
    effectsTab.setBounds (tabs.withTrimmedLeft (2));
    r.removeFromTop (8);

    hint.setBounds (r.removeFromBottom (20));
    r.removeFromBottom (4);

    if (currentTab == 0)
    {
        auto row = r.removeFromTop (24);
        previewButton.setBounds (row.removeFromRight (68));
        row.removeFromRight (6);
        locationBox.setBounds (row);
        r.removeFromTop (6);
        fileTree.setBounds (r);
    }
    else
    {
        effectList.setBounds (r);
    }
}

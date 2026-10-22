#include "UI/ProjectDialogs.h"
#include "Engine/ProjectIO.h"

using namespace Rakam::Colours;

namespace
{
    void closeDialog (juce::Component* c)
    {
        if (auto* dw = c->findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    }

    const char* describeTemplate (const juce::String& name)
    {
        if (name == "Vocal Recording") return "Beat + lead & backing vocals with EQ, compressor and reverb";
        if (name == "Podcast")         return "Host + guest with noise gate, EQ and compressor";
        if (name == "Band")            return "Drums, bass, guitar, keys and vocals";
        if (name == "Mixing 8 Stems")  return "8 tracks with EQ, mastering chain on the master";
        if (name == "Beat Making")     return "Drums, 808, melody and samples, metronome on";
        return "";
    }
}

//==============================================================================
StartScreen::StartScreen (const juce::Array<juce::File>& recent) : recentFiles (recent)
{
    templates.push_back ({ {}, {} });   // Empty project
    for (auto& n : ProjectIO::getStarterTemplateNames())
        templates.push_back ({ n, {} });
    for (auto& f : ProjectIO::getUserTemplates())
        templates.push_back ({ f.getFileNameWithoutExtension(), f });

    recentModel.owner = this;

    for (auto* list : { &templateList, &recentList })
    {
        list->setRowHeight (44);
        list->setColour (juce::ListBox::backgroundColourId, background);
        list->setColour (juce::ListBox::outlineColourId, grid);
        list->setOutlineThickness (1);
        addAndMakeVisible (list);
    }
    templateList.selectRow (0);
    if (! recentFiles.isEmpty())
        recentList.selectRow (0);

    createButton.setColour (juce::TextButton::buttonColourId, accent);
    createButton.onClick = [this]
    {
        const int row = templateList.getSelectedRow();
        if (juce::isPositiveAndBelow (row, (int) templates.size()) && onNew)
        {
            auto choice = templates[(size_t) row];
            close();
            onNew (choice);
        }
    };
    openButton.onClick = [this]
    {
        const int row = recentList.getSelectedRow();
        if (juce::isPositiveAndBelow (row, recentFiles.size()) && onOpenRecent)
        {
            auto f = recentFiles[row];
            close();
            onOpenRecent (f);
        }
    };
    openButton.setEnabled (! recentFiles.isEmpty());
    browseButton.onClick = [this] { close(); if (onOpenOther) onOpenOther(); };

    for (auto* b : { &createButton, &openButton, &browseButton })
        addAndMakeVisible (b);

    setSize (820, 520);
}

void StartScreen::close()
{
    closeDialog (this);
}

int StartScreen::getNumRows()
{
    return (int) templates.size();
}

void StartScreen::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) templates.size()))
        return;

    auto& t = templates[(size_t) row];
    if (selected)
    {
        g.setColour (accent.withAlpha (0.2f));
        g.fillRect (0, 0, width, height);
        g.setColour (accent);
        g.fillRect (0, 0, 3, height);
    }

    const auto name = t.name.isEmpty() ? juce::String ("Empty project") : t.name;
    const juce::String desc = t.name.isEmpty() ? juce::String ("Start from scratch")
                            : t.file != juce::File() ? juce::String ("Your template")
                                                     : juce::String (describeTemplate (t.name));

    g.setColour (text);
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.drawText (name, 14, 4, width - 20, 20, juce::Justification::centredLeft);
    g.setColour (textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (desc, 14, 22, width - 20, 18, juce::Justification::centredLeft, true);
}

void StartScreen::listBoxItemDoubleClicked (int, const juce::MouseEvent&)
{
    createButton.triggerClick();
}

void StartScreen::RecentModel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, owner->recentFiles.size()))
        return;

    auto f = owner->recentFiles[row];
    if (selected)
    {
        g.setColour (accent.withAlpha (0.2f));
        g.fillRect (0, 0, width, height);
        g.setColour (accent);
        g.fillRect (0, 0, 3, height);
    }

    g.setColour (text);
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.drawText (f.getFileNameWithoutExtension(), 14, 4, width - 20, 20, juce::Justification::centredLeft);
    g.setColour (textDim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (f.getLastModificationTime().formatted ("%d %b %Y %H:%M") + "   " + f.getParentDirectory().getFullPathName(),
                14, 22, width - 20, 18, juce::Justification::centredLeft, true);
}

void StartScreen::RecentModel::listBoxItemDoubleClicked (int, const juce::MouseEvent&)
{
    owner->openButton.triggerClick();
}

void StartScreen::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    g.setColour (text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText (Rakam::appName, 24, 16, 400, 32, juce::Justification::centredLeft);
    g.setColour (accent);
    g.fillRect (24, 52, 48, 3);

    g.setColour (textDim);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ("NEW PROJECT", templateList.getBounds().translated (0, -22).withHeight (18), juce::Justification::centredLeft);
    g.drawText ("RECENT PROJECTS", recentList.getBounds().translated (0, -22).withHeight (18), juce::Justification::centredLeft);

}

void StartScreen::paintOverChildren (juce::Graphics& g)
{
    if (recentFiles.isEmpty())
    {
        g.setColour (textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("No recent projects yet", recentList.getBounds(), juce::Justification::centred);
    }
}

void StartScreen::resized()
{
    auto r = getLocalBounds().reduced (24);
    r.removeFromTop (64);

    auto buttons = r.removeFromBottom (34);
    r.removeFromBottom (12);

    auto left = r.removeFromLeft (r.getWidth() / 2 - 8);
    r.removeFromLeft (16);
    templateList.setBounds (left);
    recentList.setBounds (r);

    auto lb = buttons.removeFromLeft (left.getWidth());
    createButton.setBounds (lb.removeFromRight (120));
    buttons.removeFromLeft (16);
    openButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    browseButton.setBounds (buttons.removeFromRight (170));
}

//==============================================================================
ProjectInfoDialog::ProjectInfoDialog (AudioEngine& e) : engine (e)
{
    const std::pair<const char*, juce::TextEditor*> fields[] = {
        { "Title", &title }, { "Artist", &artist }, { "Album", &album }, { "Genre", &genre }, { "Comment", &comment } };

    for (auto& [name, editor] : fields)
    {
        auto* l = labels.add (new juce::Label (juce::String(), name));
        addAndMakeVisible (l);
        addAndMakeVisible (editor);
    }

    title.setText (engine.info.title, false);
    artist.setText (engine.info.artist, false);
    album.setText (engine.info.album, false);
    genre.setText (engine.info.genre, false);
    comment.setMultiLine (true);
    comment.setReturnKeyStartsNewLine (true);
    comment.setText (engine.info.comment, false);

    note.setText ("These details are written into exported WAV, MP3 and OGG files, so they show up in music players.",
                  juce::dontSendNotification);
    note.setColour (juce::Label::textColourId, textDim);
    note.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (note);

    saveButton.setColour (juce::TextButton::buttonColourId, accent);
    saveButton.onClick = [this]
    {
        engine.info.title   = title.getText().trim();
        engine.info.artist  = artist.getText().trim();
        engine.info.album   = album.getText().trim();
        engine.info.genre   = genre.getText().trim();
        engine.info.comment = comment.getText().trim();
        engine.markEdited();
        engine.sendChangeMessage();
        closeDialog (this);
    };
    cancelButton.onClick = [this] { closeDialog (this); };
    addAndMakeVisible (saveButton);
    addAndMakeVisible (cancelButton);

    setSize (480, 390);
}

void ProjectInfoDialog::paint (juce::Graphics& g)
{
    g.fillAll (panel);
}

void ProjectInfoDialog::resized()
{
    auto r = getLocalBounds().reduced (18);
    juce::TextEditor* editors[] = { &title, &artist, &album, &genre, &comment };

    for (int i = 0; i < 5; ++i)
    {
        auto row = r.removeFromTop (i == 4 ? 80 : 28);
        r.removeFromTop (8);
        labels[i]->setBounds (row.removeFromLeft (80).withHeight (28));
        editors[i]->setBounds (row);
    }

    note.setBounds (r.removeFromTop (40));
    auto buttons = r.removeFromBottom (32);
    saveButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));
}

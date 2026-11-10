/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef NGSETTINGSPAGE_H
#define NGSETTINGSPAGE_H

#include "settings/settingspagebase.h"

class KColorButton;
class KUrlRequester;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QSlider;
class QWidget;

/**
 * dolphin-ng: settings of the fork (sidebar glass, list look). They are stored in dolphinrc,
 * group DolphinNG; a DOLPHIN_NG_* environment variable overrides the matching entry.
 * Applying them takes effect at once: ngSettingsInvalidate() tells every part of the window.
 */
class NgSettingsPage : public SettingsPageBase
{
    Q_OBJECT

public:
    explicit NgSettingsPage(QWidget *parent);
    ~NgSettingsPage() override;

    void applySettings() override;
    void restoreDefaults() override;

private:
    void loadSettings();
    void updateDependencies(); // show only the rows the chosen backdrop uses

    // sidebar
    QCheckBox *m_glassPanel;
    QComboBox *m_backdrop;
    QComboBox *m_rim;
    QCheckBox *m_devices;
    QFormLayout *m_sidebarForm;
    // sidebar: picture
    KUrlRequester *m_image;
    QSlider *m_glass;
    QSlider *m_blur;
    QCheckBox *m_motion;
    // sidebar: animated gradient
    QComboBox *m_lavaStyle;
    QSlider *m_lavaSize;
    QComboBox *m_lavaColours;
    QWidget *m_lavaColourRow;
    KColorButton *m_lavaColour[3];
    // sidebar: shared by the gradient and the moving picture
    QSlider *m_lavaSpeed;
    // list
    QCheckBox *m_twoLines;
    QComboBox *m_rows;
    QComboBox *m_mark;
    QComboBox *m_sigla;
    QCheckBox *m_icons;
    QCheckBox *m_ago;
    QFormLayout *m_listForm;
    // icons view
    QCheckBox *m_tiles;
};

#endif

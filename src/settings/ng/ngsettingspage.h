/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef NEOSETTINGSPAGE_H
#define NEOSETTINGSPAGE_H

#include "settings/settingspagebase.h"

class KColorButton;
class KUrlRequester;
class QCheckBox;
class QComboBox;
class QSlider;

/**
 * dolphin-ng: settings of the fork (sidebar picture, list look). They are stored in dolphinrc,
 * group DolphinNG; a DOLPHIN_NG_* environment variable overrides the matching entry.
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

    KUrlRequester *m_image;
    QSlider *m_glass;
    QSlider *m_blur;
    QComboBox *m_rows;
    QComboBox *m_sigla;
    QComboBox *m_mark;
    QCheckBox *m_icons;
    QCheckBox *m_tiles;
    QCheckBox *m_twoLines;
    QCheckBox *m_glassPanel;
    QComboBox *m_backdrop;
    QSlider *m_lavaSpeed;
    QSlider *m_lavaSize;
    QComboBox *m_lavaColours;
    QComboBox *m_lavaStyle;
    KColorButton *m_lavaColour[3];
    QCheckBox *m_devices;
};

#endif

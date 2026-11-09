/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef NEOSETTINGSPAGE_H
#define NEOSETTINGSPAGE_H

#include "settings/settingspagebase.h"

class KUrlRequester;
class QCheckBox;
class QComboBox;
class QSlider;

/**
 * dolphin-neo: settings of the fork (sidebar picture, list look). They are stored in dolphinrc,
 * group DolphinNeo; a DOLPHIN_NEO_* environment variable overrides the matching entry.
 */
class NeoSettingsPage : public SettingsPageBase
{
    Q_OBJECT

public:
    explicit NeoSettingsPage(QWidget *parent);
    ~NeoSettingsPage() override;

    void applySettings() override;
    void restoreDefaults() override;

private:
    void loadSettings();

    KUrlRequester *m_image;
    QSlider *m_glass;
    QSlider *m_blur;
    QComboBox *m_rows;
    QComboBox *m_sigla;
    QCheckBox *m_icons;
    QCheckBox *m_tiles;
    QCheckBox *m_twoLines;
    QCheckBox *m_glassPanel;
    QCheckBox *m_liveBlur;
    QCheckBox *m_devices;
};

#endif

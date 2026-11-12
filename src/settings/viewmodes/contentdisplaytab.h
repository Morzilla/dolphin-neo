/*
 * SPDX-FileCopyrightText: 2023 Méven Car <meven@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef GENERALTAB_H
#define GENERALTAB_H

#include "dolphin_contentdisplaysettings.h"
#include "dolphin_generalsettings.h"
#include "settings/settingspagebase.h"

class QRadioButton;
class QSpinBox;

class ContentDisplayTab : public SettingsPageBase
{
    Q_OBJECT

public:
    ContentDisplayTab(QWidget *parent);

public:
    // SettingsPageBase interface
    void applySettings() override;
    void restoreDefaults() override;

private:
    void loadSettings();
    void setSortingChoiceValue();
    void loadSortingChoiceSettings();

    QRadioButton *m_naturalSorting;
    QRadioButton *m_caseSensitiveSorting;
    QRadioButton *m_caseInsensitiveSorting;

    QRadioButton *m_numberOfItems;
    QRadioButton *m_sizeOfContents;
    int m_ngLoadedSizeMode = -1; // dolphin-ng: what the tab was opened with (see applySettings)
    int m_ngLoadedSizeLimit = -1;
    QRadioButton *m_noDirectorySize;
    QSpinBox *m_recursiveDirectorySizeLimit;
    QRadioButton *m_useRelatetiveDates;
    QRadioButton *m_useShortDates;
    QRadioButton *m_useSymbolicPermissions;
    QRadioButton *m_useNumericPermissions;
    QRadioButton *m_useCombinedPermissions;
    QRadioButton *m_elideMiddle;
    QRadioButton *m_elideEnding;
};

#endif // GENERALTAB_H

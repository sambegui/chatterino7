#pragma once

#include "widgets/settingspages/GeneralPageView.hpp"

#include <QString>
#include <QStringList>

#include <type_traits>

namespace chatterino {

#ifdef Q_OS_WIN
inline const QString META_KEY = QStringLiteral("Windows");
#else
inline const QString META_KEY = QStringLiteral("Meta");
#endif

inline const QStringList ZOOM_LEVELS = {
    "0.5x", "0.6x", "0.7x", "0.8x",  "0.9x",  "Default", "1.2x", "1.4x",
    "1.6x", "1.8x", "2x",   "2.33x", "2.66x", "3x",      "3.5x", "4x",
};

inline void addKeyboardModifierSetting(GeneralPageView &layout,
                                       const QString &title,
                                       EnumSetting<Qt::KeyboardModifier> &setting)
{
    layout.addDropdown<std::underlying_type_t<Qt::KeyboardModifier>>(
        title, {"None", "Shift", "Control", "Alt", META_KEY}, setting,
        [](int index) {
            switch (index)
            {
                case Qt::ShiftModifier:
                    return 1;
                case Qt::ControlModifier:
                    return 2;
                case Qt::AltModifier:
                    return 3;
                case Qt::MetaModifier:
                    return 4;
                default:
                    return 0;
            }
        },
        [](DropdownArgs args) {
            switch (args.index)
            {
                case 1:
                    return Qt::ShiftModifier;
                case 2:
                    return Qt::ControlModifier;
                case 3:
                    return Qt::AltModifier;
                case 4:
                    return Qt::MetaModifier;
                default:
                    return Qt::NoModifier;
            }
        },
        false);
}

}  // namespace chatterino

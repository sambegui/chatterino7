#pragma once

#include "widgets/settingspages/ScrollableSettingsPage.hpp"

namespace chatterino {

class GeneralPageView;

/// Chat behaviour: timestamps, pausing, scrolling, links and channel events.
class ChatPage : public ScrollableSettingsPage
{
    Q_OBJECT

public:
    ChatPage();

private:
    void initLayout(GeneralPageView &layout) override;
};

}  // namespace chatterino

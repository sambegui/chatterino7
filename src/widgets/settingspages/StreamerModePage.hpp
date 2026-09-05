#pragma once

#include "widgets/settingspages/ScrollableSettingsPage.hpp"

namespace chatterino {

class GeneralPageView;

/// What Chatterino hides or changes while you are streaming.
class StreamerModePage : public ScrollableSettingsPage
{
    Q_OBJECT

public:
    StreamerModePage();

private:
    void initLayout(GeneralPageView &layout) override;
};

}  // namespace chatterino

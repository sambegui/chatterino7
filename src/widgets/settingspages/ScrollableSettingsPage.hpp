#pragma once

#include "widgets/settingspages/SettingsPage.hpp"

namespace chatterino {

class GeneralPageView;

/// A settings page that hosts a scrollable, filterable GeneralPageView with a
/// navigation rail. Derived pages describe their contents in initLayout() and
/// call buildLayout() from their own constructor.
class ScrollableSettingsPage : public SettingsPage
{
    Q_OBJECT

public:
    bool filterElements(const QString &query) override;

protected:
    ScrollableSettingsPage() = default;

    /// Must be called by the derived constructor: it dispatches to initLayout,
    /// which is not available yet while the base is being constructed.
    void buildLayout();

    virtual void initLayout(GeneralPageView &layout) = 0;

    GeneralPageView *view_{};
};

}  // namespace chatterino

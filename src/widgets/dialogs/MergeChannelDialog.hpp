#pragma once

#include "common/Channel.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <functional>

namespace chatterino {

/// Type of merge view to create
enum class MergeViewType {
    /// Combined view with messages from both channels interleaved
    SingleView,
    /// Side-by-side view with separate panels for each channel
    SplitView,
};

/// Dialog for merging channels from different platforms
class MergeChannelDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MergeChannelDialog(ChannelPtr sourceChannel,
                                QWidget *parent = nullptr);

    /// Get the selected channel to merge with
    [[nodiscard]] ChannelPtr getSelectedChannel() const;

    /// Get the selected merge view type
    [[nodiscard]] MergeViewType getMergeViewType() const;

    /// Set callback for when merge is confirmed (single view - returns merged channel)
    void setOnMerge(std::function<void(ChannelPtr, ChannelPtr)> callback);

    /// Set callback for when split view is requested (returns both channels to open side-by-side)
    void setOnSplitView(std::function<void(ChannelPtr, ChannelPtr)> callback);

private:
    void setupUI();
    void updateSuggestion();
    void onMergeClicked();

    ChannelPtr sourceChannel_;
    /// Name to prefill and suggest. For a merged source this is one of
    /// its sources rather than the combined display name.
    QString suggestedName_;
    ChannelPtr selectedChannel_;
    std::function<void(ChannelPtr, ChannelPtr)> onMergeCallback_;
    std::function<void(ChannelPtr, ChannelPtr)> onSplitViewCallback_;
    MergeViewType mergeViewType_{MergeViewType::SingleView};

    // UI elements
    QVBoxLayout *mainLayout_{nullptr};
    QLabel *sourceLabel_{nullptr};
    QLabel *targetLabel_{nullptr};
    QComboBox *platformCombo_{nullptr};
    QLineEdit *channelInput_{nullptr};
    QLabel *suggestionLabel_{nullptr};

    // Merge type selection
    QLabel *mergeTypeLabel_{nullptr};
    QRadioButton *singleViewRadio_{nullptr};
    QRadioButton *splitViewRadio_{nullptr};
    QButtonGroup *mergeTypeGroup_{nullptr};
    QLabel *singleViewDescription_{nullptr};
    QLabel *splitViewDescription_{nullptr};

    QPushButton *mergeButton_{nullptr};
    QPushButton *cancelButton_{nullptr};
};

}  // namespace chatterino

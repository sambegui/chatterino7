#include "providers/youtube/YouTubeMessage.hpp"

namespace chatterino {

QString YouTubeMessage::plainText() const
{
    QString out;
    for (const auto &run : this->runs)
    {
        out += run.isEmoji() ? run.emojiLabel : run.text;
    }
    return out;
}

}  // namespace chatterino

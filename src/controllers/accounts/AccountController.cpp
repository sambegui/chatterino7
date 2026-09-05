#include "controllers/accounts/AccountController.hpp"

#include "controllers/accounts/Account.hpp"
#include "controllers/accounts/AccountModel.hpp"
#include "providers/kick/KickAccount.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "singletons/Settings.hpp"
#include "util/SharedPtrElementLess.hpp"

namespace chatterino {

AccountController::AccountController()
    : accounts_(SharedPtrElementLess<Account>{})
{
    // These signal connections can safely be ignored since the twitch object
    // will always be destroyed before the AccountController
    std::ignore =
        this->twitch.accounts.itemInserted.connect([this](const auto &args) {
            this->accounts_.insert(
                std::dynamic_pointer_cast<Account>(args.item));
        });

    std::ignore =
        this->twitch.accounts.itemRemoved.connect([this](const auto &args) {
            if (args.caller != this)
            {
                const auto &accs = this->twitch.accounts.raw();
                auto it = std::find(accs.begin(), accs.end(), args.item);
                assert(it != accs.end());

                this->accounts_.removeAt(it - accs.begin(), this);
            }
        });

    // Kick account signals
    std::ignore =
        this->kick.accounts.itemInserted.connect([this](const auto &args) {
            this->accounts_.insert(
                std::dynamic_pointer_cast<Account>(args.item));
        });

    std::ignore =
        this->kick.accounts.itemRemoved.connect([this](const auto &args) {
            if (args.caller != this)
            {
                // Find and remove from our accounts_ list
                // Note: The item has already been removed from kick.accounts
                // so we search in accounts_ instead
                auto accountPtr = std::dynamic_pointer_cast<Account>(args.item);
                if (accountPtr)
                {
                    const auto &accs = this->accounts_.raw();
                    auto it = std::find(accs.begin(), accs.end(), accountPtr);
                    if (it != accs.end())
                    {
                        this->accounts_.removeAt(it - accs.begin(), this);
                    }
                }
            }
        });

    std::ignore = this->accounts_.itemRemoved.connect([this](const auto &args) {
        switch (args.item->getProviderId())
        {
            case ProviderId::Twitch: {
                if (args.caller != this)
                {
                    auto &&accs = this->twitch.accounts;
                    auto it = std::find(accs.begin(), accs.end(), args.item);
                    assert(it != accs.end());
                    this->twitch.accounts.removeAt(it - accs.begin(), this);
                }
            }
            break;

            case ProviderId::Kick: {
                if (args.caller != this)
                {
                    // Find by raw pointer comparison since types differ
                    auto &&accs = this->kick.accounts;
                    for (size_t i = 0; i < accs.raw().size(); i++)
                    {
                        if (accs.raw()[i].get() == args.item.get())
                        {
                            this->kick.accounts.removeAt(i, this);
                            break;
                        }
                    }
                }
            }
            break;
        }
    });
}

void AccountController::load()
{
    this->twitch.load();

    // Load Kick account if Kick integration is enabled
    if (getSettings()->enableKickIntegration)
    {
        this->kick.load();
    }
}

AccountModel *AccountController::createModel(QObject *parent)
{
    AccountModel *model = new AccountModel(parent);

    model->initialize(&this->accounts_);
    return model;
}

}  // namespace chatterino

//
// The code was partially copied from https://github.com/tdlib/td
//
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#include "TdCloud.h"
TdCloud::TdCloud() {
    td::ClientManager::execute(
        td_api::make_object<td_api::setLogVerbosityLevel>(1));
    client_manager_ = std::make_unique<td::ClientManager>();
    client_id_ = client_manager_->create_client_id();
    send_query(td_api::make_object<td_api::getOption>("version"), {});
}
void TdCloud::Update() {
    while (true) {
        auto response = client_manager_->receive(0);
        if (response.object) {
            process_response(std::move(response));
        } else {
            break;
        }
    }
}
void TdCloud::Close() {
    std::cout << "Closing..." << std::endl;
    send_query(td_api::make_object<td_api::close>(), {});
}
void TdCloud::GetMyInfo() {
    std::cout << "Getting info..." << std::endl;
    send_query(td_api::make_object<td_api::getMe>(), [this](Object object) {
        std::cout << to_string(object) << std::endl;
    });
}
void TdCloud::LogOut() {
    std::cout << "Logging out..." << std::endl;
    send_query(td_api::make_object<td_api::logOut>(), {});
}
void TdCloud::LoadChats() {
    std::cout << "Loading chat list..." << std::endl;
    send_query(td_api::make_object<td_api::getChats>(nullptr, 20),
               [this](Object object) {
                   if (object->get_id() == td_api::error::ID) {
                       return;
                   }
                   auto chats = td::move_tl_object_as<td_api::chats>(object);
                   for (auto chat_id : chats->chat_ids_) {
                       std::cout << "[chat_id:" << chat_id
                                 << "] [title:" << chat_title_[chat_id] << "]"
                                 << std::endl;
                   }
               });
}
void TdCloud::SendFile(std::int64_t& chat_id, const std::string& path) {
    std::cout << "Sending file( " << path << ") to chat " << chat_id << "..."
              << std::endl;
    auto send_message = td_api::make_object<td_api::sendMessage>();
    send_message->chat_id_ = chat_id;
    td_api::make_object<td_api::inputMessageDocument>();

    auto local_file = td_api::make_object<td_api::inputFileLocal>();
    local_file->path_ = path;
    auto file_content = td_api::make_object<td_api::inputMessageDocument>();
    file_content->document_ = std::move(local_file);
    send_message->input_message_content_ = std::move(file_content);
    send_query(std::move(send_message), {});
}
void TdCloud::SendDir(std::int64_t& chat_id, const std::string& path) {
    std::cout << "Sending dir(" << path << ") to chat " << chat_id << std::endl;
    std::vector<std::string> files;
    scan_dir(path, files);
    /// TODO dir load
}
void TdCloud::loop() {
    while (true) {
        if (need_restart_) {
            restart();
        } else if (!are_authorized_) {
            process_response(client_manager_->receive(10));
        } else {
            std::cout << "[q] quit\n"
                         "[u] update\n"
                         "[c] get chats\n"
                         "[close] close\n"
                         "[l] logout\n"
                         "[me] get personal info\n"
                         "[m <chat_id> message] send message to chat_id\n"
                         "[f <chat_id> path] send file(by path) to chat_id\n"
                         "[d <chat_id> dir_path] send all the files from "
                         "dir_path to chat_id\n"
                      << std::endl;
            std::string action;
            std::cin >> action;
            if (action == "q") {
                return;
            }
            if (action == "u") {
                Update();
            } else if (action == "close") {
                Close();
            } else if (action == "me") {
                GetMyInfo();
            } else if (action == "l") {
                LogOut();
            } else if (action == "m") {
                std::int64_t chat_id;
                std::cin >> chat_id;
                std::string text;
                std::cin >> text;
                SendMessage(chat_id, text);
            } else if (action == "c") {
                LoadChats();
            } else if (action == "f") {
                std::int64_t chat_id;
                std::cin >> chat_id;
                std::string path;
                std::cin >> path;
                SendFile(chat_id, path);
            } else if (action == "d") {
                std::int64_t chat_id;
                std::cin >> chat_id;
                std::string path;
                std::cin >> path;
                SendDir(chat_id, path);
            }
        }
    }
}
void TdCloud::restart() {
    client_manager_.reset();
    *this = TdCloud();
}

void TdCloud::send_query(td_api::object_ptr<td_api::Function> f,
                         std::function<void(Object)> handler) {
    auto query_id = next_query_id();
    if (handler) {
        handlers_.emplace(query_id, std::move(handler));
    }
    client_manager_->send(client_id_, query_id, std::move(f));
}

void TdCloud::process_response(td::ClientManager::Response response) {
    if (!response.object) {
        return;
    }
    // std::cout << response.request_id << " " << to_string(response.object) <<
    // std::endl;
    if (response.request_id == 0) {
        return process_update(std::move(response.object));
    }
    auto it = handlers_.find(response.request_id);
    if (it != handlers_.end()) {
        it->second(std::move(response.object));
        handlers_.erase(it);
    }
}

std::string TdCloud::get_user_name(std::int64_t user_id) const {
    auto it = users_.find(user_id);
    if (it == users_.end()) {
        return "unknown user";
    }
    return it->second->first_name_ + " " + it->second->last_name_;
}

std::string TdCloud::get_chat_title(std::int64_t chat_id) const {
    auto it = chat_title_.find(chat_id);
    if (it == chat_title_.end()) {
        return "unknown chat";
    }
    return it->second;
}

void TdCloud::process_update(td_api::object_ptr<td_api::Object> update) {
    td_api::downcast_call(
        *update,
        overloaded(
            [this](
                td_api::updateAuthorizationState& update_authorization_state) {
                authorization_state_ =
                    std::move(update_authorization_state.authorization_state_);
                on_authorization_state_update();
            },
            [this](td_api::updateNewChat& update_new_chat) {
                chat_title_[update_new_chat.chat_->id_] =
                    update_new_chat.chat_->title_;
            },
            [this](td_api::updateChatTitle& update_chat_title) {
                chat_title_[update_chat_title.chat_id_] =
                    update_chat_title.title_;
            },
            [this](td_api::updateUser& update_user) {
                auto user_id = update_user.user_->id_;
                users_[user_id] = std::move(update_user.user_);
            },
            [this](td_api::updateNewMessage& update_new_message) {
                auto chat_id = update_new_message.message_->chat_id_;
                std::string sender_name;
                td_api::downcast_call(
                    *update_new_message.message_->sender_id_,
                    overloaded(
                        [this, &sender_name](td_api::messageSenderUser& user) {
                            sender_name = get_user_name(user.user_id_);
                        },
                        [this, &sender_name](td_api::messageSenderChat& chat) {
                            sender_name = get_chat_title(chat.chat_id_);
                        }));
                std::string text;
                if (update_new_message.message_->content_->get_id() ==
                    td_api::messageText::ID) {
                    text = static_cast<td_api::messageText&>(
                               *update_new_message.message_->content_)
                               .text_->text_;
                }
                std::cout << "Receive message: [chat_id:" << chat_id
                          << "] [from:" << sender_name << "] [" << text << "]"
                          << std::endl;
            },
            [](auto& update) {}));
}

auto TdCloud::create_authentication_query_handler() {
    return [this, id = authentication_query_id_](Object object) {
        if (id == authentication_query_id_) {
            check_authentication_error(std::move(object));
        }
    };
}

void TdCloud::on_authorization_state_update() {
    authentication_query_id_++;
    td_api::downcast_call(
        *authorization_state_,
        overloaded(
            [this](td_api::authorizationStateReady&) {
                are_authorized_ = true;
                std::cout << "Authorization is completed" << std::endl;
            },
            [this](td_api::authorizationStateLoggingOut&) {
                are_authorized_ = false;
                std::cout << "Logging out" << std::endl;
            },
            [this](td_api::authorizationStateClosing&) {
                std::cout << "Closing" << std::endl;
            },
            [this](td_api::authorizationStateClosed&) {
                are_authorized_ = false;
                need_restart_ = true;
                std::cout << "Terminated" << std::endl;
            },
            [this](td_api::authorizationStateWaitPhoneNumber&) {
                std::cout << "Enter phone number: " << std::flush;
                std::string phone_number;
                std::cin >> phone_number;
                send_query(
                    td_api::make_object<td_api::setAuthenticationPhoneNumber>(
                        phone_number, nullptr),
                    create_authentication_query_handler());
            },
            [this](td_api::authorizationStateWaitEmailAddress&) {
                std::cout << "Enter email address: " << std::flush;
                std::string email_address;
                std::cin >> email_address;
                send_query(
                    td_api::make_object<td_api::setAuthenticationEmailAddress>(
                        email_address),
                    create_authentication_query_handler());
            },
            [this](td_api::authorizationStateWaitEmailCode&) {
                std::cout << "Enter email authentication code: " << std::flush;
                std::string code;
                std::cin >> code;
                send_query(
                    td_api::make_object<td_api::checkAuthenticationEmailCode>(
                        td_api::make_object<
                            td_api::emailAddressAuthenticationCode>(code)),
                    create_authentication_query_handler());
            },
            [this](td_api::authorizationStateWaitCode&) {
                std::cout << "Enter authentication code: " << std::flush;
                std::string code;
                std::cin >> code;
                send_query(
                    td_api::make_object<td_api::checkAuthenticationCode>(code),
                    create_authentication_query_handler());
            },
            [this](td_api::authorizationStateWaitRegistration&) {
                std::string first_name;
                std::string last_name;
                std::cout << "Enter your first name: " << std::flush;
                std::cin >> first_name;
                std::cout << "Enter your last name: " << std::flush;
                std::cin >> last_name;
                send_query(td_api::make_object<td_api::registerUser>(first_name,
                                                                     last_name),
                           create_authentication_query_handler());
            },
            [this](td_api::authorizationStateWaitPassword&) {
                std::cout << "Enter authentication password: " << std::flush;
                std::string password;
                std::getline(std::cin, password);
                send_query(
                    td_api::make_object<td_api::checkAuthenticationPassword>(
                        password),
                    create_authentication_query_handler());
            },
            [this](
                td_api::authorizationStateWaitOtherDeviceConfirmation& state) {
                std::cout << "Confirm this login link on another device: "
                          << state.link_ << std::endl;
            },
            [this](td_api::authorizationStateWaitTdlibParameters&) {
                auto request =
                    td_api::make_object<td_api::setTdlibParameters>();
                request->database_directory_ = "tdlib";
                request->use_message_database_ = true;
                request->use_secret_chats_ = true;
                request->api_id_ = 94575;
                request->api_hash_ = "a3406de8d171bb422bb6ddf3bbd800e2";
                request->system_language_code_ = "en";
                request->device_model_ = "Desktop";
                request->application_version_ = "1.0";
                request->enable_storage_optimizer_ = true;
                send_query(std::move(request),
                           create_authentication_query_handler());
            }));
}

void TdCloud::check_authentication_error(Object object) {
    if (object->get_id() == td_api::error::ID) {
        auto error = td::move_tl_object_as<td_api::error>(object);
        std::cout << "Error: " << to_string(error) << std::flush;
        on_authorization_state_update();
    }
}

void TdCloud::scan_dir(const std::string& path,
                       std::vector<std::string>& files) {
    DirIterator dir_begin(path);
    DirIterator dir_end;
    for (; dir_begin != dir_end; ++dir_begin) {
        FileStatus file_status = boost::filesystem::status(*dir_begin);
        switch (file_status.type()) {
            case boost::filesystem::regular_file:
                std::cout << "FILE ";
                break;
            case boost::filesystem::symlink_file:
                std::cout << "SYMLINK ";
                break;
            case boost::filesystem::directory_file:
                scan_dir(dir_begin->path().string(), files);
                std::cout << "DIRECTORY ";
                break;
            default:
                std::cout << "OTHER ";
                break;
        }
        if (file_status.permissions() & boost::filesystem::owner_write) {
            std::cout << "W ";
        } else {
            std::cout << " ";
        }
        std::cout << *dir_begin << std::endl;
    }
}
/*
class TdCloud {
public:
    void SendMessage(const std::int64_t& chat_id, const std::string& text) {
        std::cout << "Sending message to chat " << chat_id << "..."
                  << std::endl;
        auto send_message = td_api::make_object<td_api::sendMessage>();
        send_message->chat_id_ = chat_id;
        auto message_content = td_api::make_object<td_api::inputMessageText>();
        message_content->text_ = td_api::make_object<td_api::formattedText>();
        message_content->text_->text_ = std::move(text);
        send_message->input_message_content_ = std::move(message_content);
        send_query(std::move(send_message), {});
    }
    void Update() {
        while (true) {
            auto response = client_manager_->receive(0);
            if (response.object) {
                process_response(std::move(response));
            } else {
                break;
            }
        }
    }
    void Close() {
        std::cout << "Closing..." << std::endl;
        send_query(td_api::make_object<td_api::close>(), {});
    }
    void GetMyInfo() {
        std::cout << "Getting info..." << std::endl;
        send_query(td_api::make_object<td_api::getMe>(), [this](Object object) {
            std::cout << to_string(object) << std::endl;
        });
    }
    void LogOut() {
        std::cout << "Logging out..." << std::endl;
        send_query(td_api::make_object<td_api::logOut>(), {});
    }
    void LoadChats() {
        std::cout << "Loading chat list..." << std::endl;
        send_query(td_api::make_object<td_api::getChats>(nullptr, 20),
                   [this](Object object) {
                       if (object->get_id() == td_api::error::ID) {
                           return;
                       }
                       auto chats =
                           td::move_tl_object_as<td_api::chats>(object);
                       for (auto chat_id : chats->chat_ids_) {
                           std::cout << "[chat_id:" << chat_id
                                     << "] [title:" << chat_title_[chat_id]
                                     << "]" << std::endl;
                       }
                   });
    }
    void SendFile(std::int64_t& chat_id, const std::string& path) {
        std::cout << "Sending file( " << path << ") to chat " << chat_id
                  << "..." << std::endl;
        auto send_message = td_api::make_object<td_api::sendMessage>();
        send_message->chat_id_ = chat_id;
        td_api::make_object<td_api::inputMessageDocument>();

        auto local_file = td_api::make_object<td_api::inputFileLocal>();
        local_file->path_ = path;
        auto file_content = td_api::make_object<td_api::inputMessageDocument>();
        file_content->document_ = std::move(local_file);
        send_message->input_message_content_ = std::move(file_content);
        send_query(std::move(send_message), {});
    }
    void SendDir(std::int64_t& chat_id, const std::string& path) {
        std::cout << "Sending dir(" << path << ") to chat " << chat_id
                  << std::endl;
        std::vector<std::string> files;
        scan_dir(path, files);
        /// TODO dir load
    }
    void loop() {
        while (true) {
            if (need_restart_) {
                restart();
            } else if (!are_authorized_) {
                process_response(client_manager_->receive(10));
            } else {
                std::cout
                    << "[q] quit\n"
                       "[u] update\n"
                       "[c] get chats\n"
                       "[close] close\n"
                       "[l] logout\n"
                       "[me] get personal info\n"
                       "[m <chat_id> message] send message to chat_id\n"
                       "[f <chat_id> path] send file(by path) to chat_id\n"
                       "[d <chat_id> dir_path] send all the files from "
                       "dir_path to chat_id\n"
                    << std::endl;
                std::string action;
                std::cin >> action;
                if (action == "q") {
                    return;
                }
                if (action == "u") {
                    Update();
                } else if (action == "close") {
                    Close();
                } else if (action == "me") {
                    GetMyInfo();
                } else if (action == "l") {
                    LogOut();
                } else if (action == "m") {
                    std::int64_t chat_id;
                    std::cin >> chat_id;
                    std::string text;
                    std::cin >> text;
                    SendMessage(chat_id, text);
                } else if (action == "c") {
                    LoadChats();
                } else if (action == "f") {
                    std::int64_t chat_id;
                    std::cin >> chat_id;
                    std::string path;
                    std::cin >> path;
                    SendFile(chat_id, path);
                } else if (action == "d") {
                    std::int64_t chat_id;
                    std::cin >> chat_id;
                    std::string path;
                    std::cin >> path;
                    SendDir(chat_id, path);
                }
            }
        }
    }

private:
    using Object = td_api::object_ptr<td_api::Object>;
    using DirIterator = boost::filesystem::directory_iterator;
    using FileStatus = boost::filesystem::file_status;
    std::unique_ptr<td::ClientManager> client_manager_;
    std::int32_t client_id_{0};

    td_api::object_ptr<td_api::AuthorizationState> authorization_state_;
    bool are_authorized_{false};
    bool need_restart_{false};
    std::uint64_t current_query_id_{0};
    std::uint64_t authentication_query_id_{0};

    std::map<std::uint64_t, std::function<void(Object)>> handlers_;

    std::map<std::int64_t, td_api::object_ptr<td_api::user>> users_;

    std::map<std::int64_t, std::string> chat_title_;

    void restart() {
        client_manager_.reset();
        *this = TdCloud();
    }

    void send_query(td_api::object_ptr<td_api::Function> f,
                    std::function<void(Object)> handler) {
        auto query_id = next_query_id();
        if (handler) {
            handlers_.emplace(query_id, std::move(handler));
        }
        client_manager_->send(client_id_, query_id, std::move(f));
    }

    void process_response(td::ClientManager::Response response) {
        if (!response.object) {
            return;
        }
        // std::cout << response.request_id << " " << to_string(response.object) <<
        // std::endl;
        if (response.request_id == 0) {
            return process_update(std::move(response.object));
        }
        auto it = handlers_.find(response.request_id);
        if (it != handlers_.end()) {
            it->second(std::move(response.object));
            handlers_.erase(it);
        }
    }

    std::string get_user_name(std::int64_t user_id) const {
        auto it = users_.find(user_id);
        if (it == users_.end()) {
            return "unknown user";
        }
        return it->second->first_name_ + " " + it->second->last_name_;
    }

    std::string get_chat_title(std::int64_t chat_id) const {
        auto it = chat_title_.find(chat_id);
        if (it == chat_title_.end()) {
            return "unknown chat";
        }
        return it->second;
    }

    void process_update(td_api::object_ptr<td_api::Object> update) {
        td_api::downcast_call(
            *update,
            overloaded(
                [this](td_api::updateAuthorizationState&
                           update_authorization_state) {
                    authorization_state_ = std::move(
                        update_authorization_state.authorization_state_);
                    on_authorization_state_update();
                },
                [this](td_api::updateNewChat& update_new_chat) {
                    chat_title_[update_new_chat.chat_->id_] =
                        update_new_chat.chat_->title_;
                },
                [this](td_api::updateChatTitle& update_chat_title) {
                    chat_title_[update_chat_title.chat_id_] =
                        update_chat_title.title_;
                },
                [this](td_api::updateUser& update_user) {
                    auto user_id = update_user.user_->id_;
                    users_[user_id] = std::move(update_user.user_);
                },
                [this](td_api::updateNewMessage& update_new_message) {
                    auto chat_id = update_new_message.message_->chat_id_;
                    std::string sender_name;
                    td_api::downcast_call(
                        *update_new_message.message_->sender_id_,
                        overloaded(
                            [this,
                             &sender_name](td_api::messageSenderUser& user) {
                                sender_name = get_user_name(user.user_id_);
                            },
                            [this,
                             &sender_name](td_api::messageSenderChat& chat) {
                                sender_name = get_chat_title(chat.chat_id_);
                            }));
                    std::string text;
                    if (update_new_message.message_->content_->get_id() ==
                        td_api::messageText::ID) {
                        text = static_cast<td_api::messageText&>(
                                   *update_new_message.message_->content_)
                                   .text_->text_;
                    }
                    std::cout << "Receive message: [chat_id:" << chat_id
                              << "] [from:" << sender_name << "] [" << text
                              << "]" << std::endl;
                },
                [](auto& update) {}));
    }

    auto create_authentication_query_handler() {
        return [this, id = authentication_query_id_](Object object) {
            if (id == authentication_query_id_) {
                check_authentication_error(std::move(object));
            }
        };
    }

    void on_authorization_state_update() {
        authentication_query_id_++;
        td_api::downcast_call(
            *authorization_state_,
            overloaded(
                [this](td_api::authorizationStateReady&) {
                    are_authorized_ = true;
                    std::cout << "Authorization is completed" << std::endl;
                },
                [this](td_api::authorizationStateLoggingOut&) {
                    are_authorized_ = false;
                    std::cout << "Logging out" << std::endl;
                },
                [this](td_api::authorizationStateClosing&) {
                    std::cout << "Closing" << std::endl;
                },
                [this](td_api::authorizationStateClosed&) {
                    are_authorized_ = false;
                    need_restart_ = true;
                    std::cout << "Terminated" << std::endl;
                },
                [this](td_api::authorizationStateWaitPhoneNumber&) {
                    std::cout << "Enter phone number: " << std::flush;
                    std::string phone_number;
                    std::cin >> phone_number;
                    send_query(td_api::make_object<
                                   td_api::setAuthenticationPhoneNumber>(
                                   phone_number, nullptr),
                               create_authentication_query_handler());
                },
                [this](td_api::authorizationStateWaitEmailAddress&) {
                    std::cout << "Enter email address: " << std::flush;
                    std::string email_address;
                    std::cin >> email_address;
                    send_query(td_api::make_object<
                                   td_api::setAuthenticationEmailAddress>(
                                   email_address),
                               create_authentication_query_handler());
                },
                [this](td_api::authorizationStateWaitEmailCode&) {
                    std::cout << "Enter email authentication code: "
                              << std::flush;
                    std::string code;
                    std::cin >> code;
                    send_query(
                        td_api::make_object<
                            td_api::checkAuthenticationEmailCode>(
                            td_api::make_object<
                                td_api::emailAddressAuthenticationCode>(code)),
                        create_authentication_query_handler());
                },
                [this](td_api::authorizationStateWaitCode&) {
                    std::cout << "Enter authentication code: " << std::flush;
                    std::string code;
                    std::cin >> code;
                    send_query(
                        td_api::make_object<td_api::checkAuthenticationCode>(
                            code),
                        create_authentication_query_handler());
                },
                [this](td_api::authorizationStateWaitRegistration&) {
                    std::string first_name;
                    std::string last_name;
                    std::cout << "Enter your first name: " << std::flush;
                    std::cin >> first_name;
                    std::cout << "Enter your last name: " << std::flush;
                    std::cin >> last_name;
                    send_query(td_api::make_object<td_api::registerUser>(
                                   first_name, last_name),
                               create_authentication_query_handler());
                },
                [this](td_api::authorizationStateWaitPassword&) {
                    std::cout << "Enter authentication password: "
                              << std::flush;
                    std::string password;
                    std::getline(std::cin, password);
                    send_query(
                        td_api::make_object<
                            td_api::checkAuthenticationPassword>(password),
                        create_authentication_query_handler());
                },
                [this](td_api::authorizationStateWaitOtherDeviceConfirmation&
                           state) {
                    std::cout << "Confirm this login link on another device: "
                              << state.link_ << std::endl;
                },
                [this](td_api::authorizationStateWaitTdlibParameters&) {
                    auto request =
                        td_api::make_object<td_api::setTdlibParameters>();
                    request->database_directory_ = "tdlib";
                    request->use_message_database_ = true;
                    request->use_secret_chats_ = true;
                    request->api_id_ = 94575;
                    request->api_hash_ = "a3406de8d171bb422bb6ddf3bbd800e2";
                    request->system_language_code_ = "en";
                    request->device_model_ = "Desktop";
                    request->application_version_ = "1.0";
                    request->enable_storage_optimizer_ = true;
                    send_query(std::move(request),
                               create_authentication_query_handler());
                }));
    }

    void check_authentication_error(Object object) {
        if (object->get_id() == td_api::error::ID) {
            auto error = td::move_tl_object_as<td_api::error>(object);
            std::cout << "Error: " << to_string(error) << std::flush;
            on_authorization_state_update();
        }
    }

    std::uint64_t next_query_id() { return ++current_query_id_; }
    void scan_dir(const std::string& path, std::vector<std::string>& files) {
        DirIterator dir_begin(path);
        DirIterator dir_end;
        for (; dir_begin != dir_end; ++dir_begin) {
            FileStatus file_status = boost::filesystem::status(*dir_begin);
            switch (file_status.type()) {
                case boost::filesystem::regular_file:
                    std::cout << "FILE ";
                    break;
                case boost::filesystem::symlink_file:
                    std::cout << "SYMLINK ";
                    break;
                case boost::filesystem::directory_file:
                    scan_dir(dir_begin->path().string(), files);
                    std::cout << "DIRECTORY ";
                    break;
                default:
                    std::cout << "OTHER ";
                    break;
            }
            if (file_status.permissions() & boost::filesystem::owner_write) {
                std::cout << "W ";
            } else {
                std::cout << " ";
            }
            std::cout << *dir_begin << std::endl;
        }
    }
};
*/
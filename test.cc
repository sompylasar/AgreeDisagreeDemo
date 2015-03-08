/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2015 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*******************************************************************************/

#include "../Bricks/port.h"

#include <string>

#include "../Bricks/cerealize/cerealize.h"
#include "../Bricks/util/singleton.h"
#include "../Bricks/strings/printf.h"
#include "../Bricks/net/api/api.h"
#include "../Bricks/dflags/dflags.h"
#include "../Bricks/3party/gtest/gtest-main-with-dflags.h"

using bricks::Singleton;
using bricks::strings::Printf;

DEFINE_int32(test_port, 3000, "Local port to use for the test.");

// Low-level storage layer and data schema for `AgreeDisagreeDemo`.

namespace storage {

typedef std::string UID;
enum class QID : size_t { NONE = 0 };

class AgreeDisagreeStorage final {
 public:
  struct Question {
    Question() {}
    QID qid;
    size_t id;
    std::string text;
    template <typename A>
    void serialize(A& ar) {
      ar(CEREAL_NVP(qid), CEREAL_NVP(text));
    }
  };

  struct User {
    User() {}
    UID uid;
    std::map<QID, bool> answers;
    template <typename A>
    void serialize(A& ar) {
      ar(CEREAL_NVP(uid), CEREAL_NVP(answers));
    }
  };

  // Registers HTTP endpoints for the provided client name.
  // Ensures that questions indexing will start from 1 by adding a dummy question with index 0.
  explicit AgreeDisagreeStorage(const std::string& client_name)
      : client_name_(client_name), questions_({Question()}), questions_reverse_index_({{"", QID::NONE}}) {
    HTTP(FLAGS_test_port).Register("/" + client_name_, [](Request r) { r("OK\n"); });
    HTTP(FLAGS_test_port).Register("/" + client_name_ + "/q",
                                   std::bind(&AgreeDisagreeStorage::HandleQ, this, std::placeholders::_1));
    HTTP(FLAGS_test_port).Register("/" + client_name_ + "/u",
                                   std::bind(&AgreeDisagreeStorage::HandleU, this, std::placeholders::_1));
  }

  // Unregisters HTTP endpoints.
  ~AgreeDisagreeStorage() {
    HTTP(FLAGS_test_port).UnRegister("/" + client_name_);
    HTTP(FLAGS_test_port).UnRegister("/" + client_name_ + "/q");
    HTTP(FLAGS_test_port).UnRegister("/" + client_name_ + "/u");
  }

 private:
  // Retrieves or creates questions.
  void HandleQ(Request r) {
    if (r.method == "GET") {
      const QID qid = static_cast<QID>(atoi(r.url.query["qid"].c_str()));
      if (qid == QID::NONE) {
        r("NEED QID\n", HTTPResponseCode.BadRequest);
      } else if (static_cast<size_t>(qid) >= questions_.size()) {
        r("QUESTION NOT FOUND\n", HTTPResponseCode.NotFound);
      } else {
        r(questions_[static_cast<size_t>(qid)]);
      }
    } else if (r.method == "POST") {
      const std::string text = r.url.query["text"];
      if (text.empty()) {
        r("NEED TEXT\n", HTTPResponseCode.BadRequest);
      } else if (questions_reverse_index_.count(text)) {
        r("DUPLICATE QUESTION\n", HTTPResponseCode.BadRequest);
      } else {
        const QID qid = static_cast<QID>(questions_.size());
        questions_.push_back(Question());
        Question& new_question = questions_.back();
        new_question.qid = qid;
        new_question.text = text;
        questions_reverse_index_[text] = qid;
        r(new_question, "question");
      }
    } else {
      r("METHOD NOT ALLOWED\n", HTTPResponseCode.MethodNotAllowed);
    }
  }

  // Retrieves or creates users.
  void HandleU(Request r) {
    const UID uid = r.url.query["uid"];
    if (uid.empty()) {
      r("NEED UID\n", HTTPResponseCode.BadRequest);
    } else {
      if (r.method == "GET") {
        const auto cit = users_.find(uid);
        if (cit != users_.end()) {
          r(cit->second, "user");
        } else {
          r("USER NOT FOUND\n", HTTPResponseCode.NotFound);
        }
      } else if (r.method == "POST") {
        const auto cit = users_.find(uid);
        if (cit != users_.end()) {
          r("CANNOT READD USER\n", HTTPResponseCode.BadRequest);
        } else {
          User& new_user = users_[uid];
          new_user.uid = uid;
          r(new_user, "user");
        }
      } else {
        r("METHOD NOT ALLOWED\n", HTTPResponseCode.MethodNotAllowed);
      }
    }
  }

  const std::string client_name_;

  std::vector<Question> questions_;
  std::map<std::string, QID> questions_reverse_index_;

  std::map<UID, User> users_;

  AgreeDisagreeStorage() = delete;
  AgreeDisagreeStorage(const AgreeDisagreeStorage&) = delete;
  AgreeDisagreeStorage(AgreeDisagreeStorage&&) = delete;
  void operator=(const AgreeDisagreeStorage&) = delete;
  void operator=(AgreeDisagreeStorage&&) = delete;
};

}  // namespace storage

struct ListenOnTestPort {
  ListenOnTestPort() {
    HTTP(FLAGS_test_port).Register("/", [](Request r) { r("I'm listening, baby.\n"); });
  }
};

TEST(AgreeDisagreeDemo, EndpointsAndScope) {
  const std::string url_prefix = Printf("http://localhost:%d", FLAGS_test_port);
  // `/test1` is inactive. Ensure that an HTTP server is listening on the port though.
  Singleton<ListenOnTestPort>();
  EXPECT_EQ(404, static_cast<int>(HTTP(GET(url_prefix + "/test1")).code));
  {
    storage::AgreeDisagreeStorage storage("test1");
    // `/test1` is available in the scope of `AgreeDisagreeStorage("test1");`.
    EXPECT_EQ(200, static_cast<int>(HTTP(GET(url_prefix + "/test1")).code));
  }
  // `/test1` is inactive again as `AgreeDisagreeStorage("test1");` gets out of scope.
  EXPECT_EQ(404, static_cast<int>(HTTP(GET(url_prefix + "/test1")).code));
}

TEST(AgreeDisagreeDemo, Questions) {
  storage::AgreeDisagreeStorage storage("test2");
  const std::string url_prefix = Printf("http://localhost:%d", FLAGS_test_port);
  // The question with QID=1 does not exist.
  EXPECT_EQ(404, static_cast<int>(HTTP(GET(url_prefix + "/test2/q?qid=1")).code));
  // A question can be added and gets a QID of 1.
  const auto added = HTTP(POST(url_prefix + "/test2/q?text=Why%3F"));
  EXPECT_EQ(200, static_cast<int>(added.code));
  EXPECT_EQ("{\"question\":{\"qid\":1,\"text\":\"Why?\"}}\n", added.body);
  // A new question with the same text can not be added.
  EXPECT_EQ(400, static_cast<int>(HTTP(POST(url_prefix + "/test2/q?text=Why%3F")).code));
  // A question with QID of 1 can be retrieved now.
  const auto retrieved = HTTP(GET(url_prefix + "/test2/q?qid=1"));
  EXPECT_EQ(200, static_cast<int>(retrieved.code));
  EXPECT_EQ("{\"value0\":{\"qid\":1,\"text\":\"Why?\"}}\n", retrieved.body);
}

TEST(AgreeDisagreeDemo, Users) {
  storage::AgreeDisagreeStorage storage("test3");
  const std::string url_prefix = Printf("http://localhost:%d", FLAGS_test_port);
  // The user "adam" does not exist.
  EXPECT_EQ(404, static_cast<int>(HTTP(GET(url_prefix + "/test3/u?uid=adam")).code));
  // The user "adam" can be added.
  const auto added = HTTP(POST(url_prefix + "/test3/u?uid=adam"));
  EXPECT_EQ(200, static_cast<int>(added.code));
  EXPECT_EQ("{\"user\":{\"uid\":\"adam\",\"answers\":[]}}\n", added.body);
  // The user "adam" cannot be re-added.
  EXPECT_EQ(400, static_cast<int>(HTTP(POST(url_prefix + "/test3/u?uid=adam")).code));
  // The user "adam" exists now.
  EXPECT_EQ(200, static_cast<int>(HTTP(GET(url_prefix + "/test3/u?uid=adam")).code));
}

#include "../include/trip/qt_trip_client.hpp"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaObject>
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>

namespace trip
{
    namespace
    {
        QList<QPair<QString, QString>> toPairs(
            std::initializer_list<std::pair<const char *, const QString &>> fields)
        {
            QList<QPair<QString, QString>> pairs;
            pairs.reserve(static_cast<qsizetype>(fields.size()));
            for (const auto &field : fields)
            {
                pairs.push_back({QString::fromUtf8(field.first), field.second});
            }
            return pairs;
        }

        QString joinCsv(const QStringList &items)
        {
            return items.join(QLatin1Char(','));
        }

        void setBearerToken(QNetworkRequest &request, const QString &token)
        {
            if (!token.isEmpty())
            {
                request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
            }
        }

        QtApiResult executeBlockingRequest(
            QNetworkAccessManager &network,
            QNetworkRequest request,
            const QByteArray &body)
        {
            QNetworkReply *reply = body.isEmpty() ? network.get(request) : network.post(request, body);

            QEventLoop loop;
            QTimer timer;
            bool timed_out = false;
            timer.setSingleShot(true);

            QObject::connect(reply, &QNetworkReply::finished, &loop, [&loop, &timer]()
                             {
                timer.stop();
                loop.quit(); });
            QObject::connect(&timer, &QTimer::timeout, &loop, [&]()
                             {
                timed_out = true;
                reply->abort();
                loop.quit(); });
            timer.start(5000);
            loop.exec();

            QtApiResult result;
            result.http_status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (timed_out)
            {
                result.status = QStringLiteral("TransportError");
                result.message = QStringLiteral("Request timed out");
                result.ok = false;
                reply->deleteLater();
                return result;
            }

            const QByteArray response_bytes = reply->readAll();
            QJsonParseError parse_error;
            const QJsonDocument doc = QJsonDocument::fromJson(response_bytes, &parse_error);

            if (parse_error.error != QJsonParseError::NoError || !doc.isObject())
            {
                result.status = QStringLiteral("TransportError");
                result.message = reply->errorString().isEmpty()
                                     ? QString::fromUtf8(response_bytes)
                                     : reply->errorString();
                result.ok = false;
                reply->deleteLater();
                return result;
            }

            result.payload = doc.object();
            result.status = result.payload.value(QStringLiteral("status")).toString();
            result.message = result.payload.value(QStringLiteral("message")).toString();
            result.ok = (result.status == QStringLiteral("Ok"));

            reply->deleteLater();
            return result;
        }
    }

    QtTripClient::QtTripClient(QString base_url)
        : base_url_(std::move(base_url))
    {
    }

    void QtTripClient::setBaseUrl(const QString &base_url)
    {
        base_url_ = base_url;
    }

    QString QtTripClient::baseUrl() const
    {
        return base_url_;
    }

    QtApiResult QtTripClient::health()
    {
        return get(QStringLiteral("/health"));
    }

    QtApiResult QtTripClient::registerUser(const QString &login, const QString &password)
    {
        return postForm(QStringLiteral("/register"), toPairs({{"login", login}, {"password", password}}));
    }

    QtApiResult QtTripClient::login(const QString &login, const QString &password)
    {
        return postForm(QStringLiteral("/login"), toPairs({{"login", login}, {"password", password}}));
    }

    QtApiResult QtTripClient::listTrips(const QString &token)
    {
        return get(QStringLiteral("/trips/list"), {}, token);
    }

    QtApiResult QtTripClient::createTrip(
        const QString &token,
        const QString &title,
        const QString &start_date,
        const QString &end_date,
        const QString &description)
    {
        return postForm(
            QStringLiteral("/trips/create"),
            toPairs({
                {"title", title},
                {"start_date", start_date},
                {"end_date", end_date},
                {"description", description}}),
            token);
    }

    QtApiResult QtTripClient::deleteTrip(const QString &token, const QString &trip_id)
    {
        return postForm(
            QStringLiteral("/trips/delete"),
            toPairs({{"trip_id", trip_id}}),
            token);
    }

    QtApiResult QtTripClient::updateTripInfo(
        const QString &token,
        const QString &trip_id,
        quint64 expected_revision,
        const QString &title,
        const QString &start_date,
        const QString &end_date,
        const QString &description)
    {
        return postForm(
            QStringLiteral("/trips/update_info"),
            toPairs({
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"title", title},
                {"start_date", start_date},
                {"end_date", end_date},
                {"description", description}}),
            token);
    }

    QtApiResult QtTripClient::createInvite(const QString &token, const QString &trip_id, const QString &role)
    {
        return postForm(
            QStringLiteral("/invites/create"),
            toPairs({{"trip_id", trip_id}, {"role", role}}),
            token);
    }

    QtApiResult QtTripClient::acceptInvite(const QString &token, const QString &invite_code)
    {
        return postForm(
            QStringLiteral("/invites/accept"),
            toPairs({{"invite_code", invite_code}}),
            token);
    }

    QtApiResult QtTripClient::changeMemberRole(
        const QString &token,
        const QString &trip_id,
        const QString &target_user_id,
        const QString &new_role)
    {
        return postForm(
            QStringLiteral("/members/change_role"),
            toPairs({
                {"trip_id", trip_id},
                {"target_user_id", target_user_id},
                {"new_role", new_role}}),
            token);
    }

    QtApiResult QtTripClient::removeMember(const QString &token, const QString &trip_id, const QString &target_user_id)
    {
        return postForm(
            QStringLiteral("/members/remove"),
            toPairs({
                {"trip_id", trip_id},
                {"target_user_id", target_user_id}}),
            token);
    }

    QtApiResult QtTripClient::getRevision(const QString &token, const QString &trip_id)
    {
        return get(QStringLiteral("/trips/revision"), toPairs({{"trip_id", trip_id}}), token);
    }

    QtApiResult QtTripClient::getSnapshot(const QString &token, const QString &trip_id)
    {
        return get(QStringLiteral("/trips/snapshot"), toPairs({{"trip_id", trip_id}}), token);
    }

    QtApiResult QtTripClient::getEventsSince(const QString &token, const QString &trip_id, quint64 since_revision)
    {
        return get(
            QStringLiteral("/events/since"),
            toPairs({
                {"trip_id", trip_id},
                {"since_revision", QString::number(since_revision)}}),
            token);
    }

    QtApiResult QtTripClient::addDay(const QString &token, const QString &trip_id, quint64 expected_revision, const QString &day_name)
    {
        return postForm(
            QStringLiteral("/days/add"),
            toPairs({
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"day_name", day_name}}),
            token);
    }

    QtApiResult QtTripClient::renameDay(
        const QString &token,
        const QString &trip_id,
        quint64 expected_revision,
        const QString &day_id,
        const QString &new_name)
    {
        return postForm(
            QStringLiteral("/days/rename"),
            toPairs({
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"day_id", day_id},
                {"new_name", new_name}}),
            token);
    }

    QtApiResult QtTripClient::removeDay(
        const QString &token,
        const QString &trip_id,
        quint64 expected_revision,
        const QString &day_id)
    {
        return postForm(
            QStringLiteral("/days/remove"),
            toPairs({
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"day_id", day_id}}),
            token);
    }

    QtApiResult QtTripClient::reorderDays(
        const QString &token,
        const QString &trip_id,
        quint64 expected_revision,
        const QStringList &day_ids_order)
    {
        return postForm(
            QStringLiteral("/days/reorder"),
            toPairs({
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"day_ids_order", joinCsv(day_ids_order)}}),
            token);
    }

    QtApiResult QtTripClient::addPlanItem(
        const QString &token,
        const QString &trip_id,
        const QString &day_id,
        quint64 expected_revision,
        const QString &name,
        const QString &time,
        const QString &notes,
        const QString &category,
        const QString &link)
    {
        return postForm(
            QStringLiteral("/plan/add"),
            toPairs({
                {"trip_id", trip_id},
                {"day_id", day_id},
                {"expected_revision", QString::number(expected_revision)},
                {"name", name},
                {"time", time},
                {"notes", notes},
                {"category", category},
                {"link", link}}),
            token);
    }

    QtApiResult QtTripClient::updatePlanItem(
        const QString &token,
        const QString &trip_id,
        const QString &day_id,
        quint64 expected_revision,
        const QString &item_id,
        const QString &name,
        const QString &time,
        const QString &notes,
        const QString &category,
        const QString &link)
    {
        return postForm(
            QStringLiteral("/plan/update"),
            toPairs({
                {"trip_id", trip_id},
                {"day_id", day_id},
                {"expected_revision", QString::number(expected_revision)},
                {"item_id", item_id},
                {"name", name},
                {"time", time},
                {"notes", notes},
                {"category", category},
                {"link", link}}),
            token);
    }

    QtApiResult QtTripClient::removePlanItem(
        const QString &token,
        const QString &trip_id,
        const QString &day_id,
        quint64 expected_revision,
        const QString &item_id)
    {
        return postForm(
            QStringLiteral("/plan/remove"),
            toPairs({
                {"trip_id", trip_id},
                {"day_id", day_id},
                {"expected_revision", QString::number(expected_revision)},
                {"item_id", item_id}}),
            token);
    }

    QtApiResult QtTripClient::reorderPlanItems(
        const QString &token,
        const QString &trip_id,
        const QString &day_id,
        quint64 expected_revision,
        const QStringList &item_ids_order)
    {
        return postForm(
            QStringLiteral("/plan/reorder"),
            toPairs({
                {"trip_id", trip_id},
                {"day_id", day_id},
                {"expected_revision", QString::number(expected_revision)},
                {"item_ids_order", joinCsv(item_ids_order)}}),
            token);
    }

    QtApiResult QtTripClient::addTask(
        const QString &token,
        const QString &trip_id,
        quint64 expected_revision,
        const QString &text,
        const QString &assignee_user_id,
        const QString &deadline)
    {
        return postForm(
            QStringLiteral("/tasks/add"),
            toPairs({
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"text", text},
                {"assignee_user_id", assignee_user_id},
                {"deadline", deadline}}),
            token);
    }

    QtApiResult QtTripClient::updateTask(
        const QString &token,
        const QString &trip_id,
        quint64 expected_revision,
        const QString &task_id,
        const QString &text,
        bool done,
        const QString &assignee_user_id,
        const QString &deadline)
    {
        return postForm(
            QStringLiteral("/tasks/update"),
            toPairs({
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"task_id", task_id},
                {"text", text},
                {"done", done ? QStringLiteral("true") : QStringLiteral("false")},
                {"assignee_user_id", assignee_user_id},
                {"deadline", deadline}}),
            token);
    }

    QtApiResult QtTripClient::setTaskDone(
        const QString &token,
        const QString &trip_id,
        quint64 expected_revision,
        const QString &task_id,
        bool done)
    {
        return postForm(
            QStringLiteral("/tasks/set_done"),
            toPairs({
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"task_id", task_id},
                {"done", done ? QStringLiteral("true") : QStringLiteral("false")}}),
            token);
    }

    QtApiResult QtTripClient::removeTask(
        const QString &token,
        const QString &trip_id,
        quint64 expected_revision,
        const QString &task_id)
    {
        return postForm(
            QStringLiteral("/tasks/remove"),
            toPairs({
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"task_id", task_id}}),
            token);
    }

    QtApiResult QtTripClient::setBudgetSettings(
        const QString &token,
        const QString &trip_id,
        quint64 expected_revision,
        const QString &currency,
        double total_limit)
    {
        return postForm(
            QStringLiteral("/budget/settings"),
            toPairs({
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"currency", currency},
                {"total_limit", QString::number(total_limit, 'f', 2)}}),
            token);
    }

    QtApiResult QtTripClient::addExpense(
        const QString &token,
        const QString &trip_id,
        quint64 expected_revision,
        double amount,
        const QString &category,
        const QString &paid_by_user_id,
        const QString &comment,
        const QString &date,
        const QString &day_id)
    {
        return postForm(
            QStringLiteral("/budget/add_expense"),
            toPairs({
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"amount", QString::number(amount, 'f', 2)},
                {"category", category},
                {"paid_by_user_id", paid_by_user_id},
                {"comment", comment},
                {"date", date},
                {"day_id", day_id}}),
            token);
    }

    QtApiResult QtTripClient::getBudgetSummary(const QString &token, const QString &trip_id)
    {
        return get(QStringLiteral("/budget/summary"), toPairs({{"trip_id", trip_id}}), token);
    }

    QtApiResult QtTripClient::sendChatMessage(const QString &token, const QString &trip_id, const QString &text)
    {
        return postForm(
            QStringLiteral("/chat/send"),
            toPairs({{"trip_id", trip_id}, {"text", text}}),
            token);
    }

    QtApiResult QtTripClient::listChatMessages(const QString &token, const QString &trip_id)
    {
        return get(QStringLiteral("/chat/list"), toPairs({{"trip_id", trip_id}}), token);
    }

    QtApiResult QtTripClient::searchInTrip(const QString &token, const QString &trip_id, const QString &query)
    {
        return get(
            QStringLiteral("/search"),
            toPairs({{"trip_id", trip_id}, {"query", query}}),
            token);
    }

    QtApiResult QtTripClient::exportTripJson(const QString &token, const QString &trip_id)
    {
        return get(QStringLiteral("/trips/export_json"), toPairs({{"trip_id", trip_id}}), token);
    }

    QtApiResult QtTripClient::importTripJson(const QString &token, const QString &trip_json)
    {
        return postForm(
            QStringLiteral("/trips/import_json"),
            toPairs({{"trip_json", trip_json}}),
            token);
    }

    QNetworkRequest QtTripClient::updatesWebSocketRequest(
        const QString &token,
        const QString &trip_id,
        quint64 since_revision) const
    {
        QUrl url = makeUrl(QStringLiteral("/ws/updates"));
        if (url.scheme() == QStringLiteral("https"))
        {
            url.setScheme(QStringLiteral("wss"));
        }
        else
        {
            url.setScheme(QStringLiteral("ws"));
        }

        QUrlQuery query;
        query.addQueryItem(QStringLiteral("trip_id"), trip_id);
        query.addQueryItem(QStringLiteral("since_revision"), QString::number(since_revision));
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("trip-qt-client"));
        setBearerToken(request, token);
        return request;
    }

    QtApiResult QtTripClient::get(const QString &path, const QList<QPair<QString, QString>> &query_items, const QString &token)
    {
        QUrl url = makeUrl(path);
        QUrlQuery query;
        for (const auto &query_item : query_items)
        {
            query.addQueryItem(query_item.first, query_item.second);
        }
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("trip-qt-client"));
        setBearerToken(request, token);
        return runRequest(std::move(request));
    }

    QtApiResult QtTripClient::postForm(
        const QString &path,
        const QList<QPair<QString, QString>> &form_items,
        const QString &token)
    {
        QUrlQuery form;
        for (const auto &form_item : form_items)
        {
            form.addQueryItem(form_item.first, form_item.second);
        }

        QNetworkRequest request(makeUrl(path));
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("trip-qt-client"));
        request.setHeader(
            QNetworkRequest::ContentTypeHeader,
            QStringLiteral("application/x-www-form-urlencoded"));
        setBearerToken(request, token);

        return runRequest(std::move(request), form.toString(QUrl::FullyEncoded).toUtf8());
    }

    QtApiResult QtTripClient::runRequest(QNetworkRequest request, const QByteArray &body)
    {
        QtApiResult result;
        QThread worker_thread;
        QObject worker_context;
        worker_context.moveToThread(&worker_thread);
        worker_thread.start();

        QMetaObject::invokeMethod(
            &worker_context,
            [&result, request, body]()
            {
                QNetworkAccessManager network;
                result = executeBlockingRequest(network, request, body);
            },
            Qt::BlockingQueuedConnection);

        worker_thread.quit();
        worker_thread.wait();
        return result;
    }

    QUrl QtTripClient::makeUrl(const QString &path) const
    {
        QString normalized = base_url_;
        if (normalized.endsWith(QLatin1Char('/')))
        {
            normalized.chop(1);
        }
        return QUrl(normalized + path);
    }
}

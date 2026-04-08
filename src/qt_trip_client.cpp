#include "../include/trip/qt_trip_client.hpp"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
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
        return get(QStringLiteral("/trips/list"), toPairs({{"token", token}}));
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
                {"token", token},
                {"title", title},
                {"start_date", start_date},
                {"end_date", end_date},
                {"description", description}}));
    }

    QtApiResult QtTripClient::deleteTrip(const QString &token, const QString &trip_id)
    {
        return postForm(
            QStringLiteral("/trips/delete"),
            toPairs({{"token", token}, {"trip_id", trip_id}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"title", title},
                {"start_date", start_date},
                {"end_date", end_date},
                {"description", description}}));
    }

    QtApiResult QtTripClient::createInvite(const QString &token, const QString &trip_id, const QString &role)
    {
        return postForm(
            QStringLiteral("/invites/create"),
            toPairs({{"token", token}, {"trip_id", trip_id}, {"role", role}}));
    }

    QtApiResult QtTripClient::acceptInvite(const QString &token, const QString &invite_code)
    {
        return postForm(
            QStringLiteral("/invites/accept"),
            toPairs({{"token", token}, {"invite_code", invite_code}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"target_user_id", target_user_id},
                {"new_role", new_role}}));
    }

    QtApiResult QtTripClient::removeMember(const QString &token, const QString &trip_id, const QString &target_user_id)
    {
        return postForm(
            QStringLiteral("/members/remove"),
            toPairs({
                {"token", token},
                {"trip_id", trip_id},
                {"target_user_id", target_user_id}}));
    }

    QtApiResult QtTripClient::getRevision(const QString &token, const QString &trip_id)
    {
        return get(QStringLiteral("/trips/revision"), toPairs({{"token", token}, {"trip_id", trip_id}}));
    }

    QtApiResult QtTripClient::getSnapshot(const QString &token, const QString &trip_id)
    {
        return get(QStringLiteral("/trips/snapshot"), toPairs({{"token", token}, {"trip_id", trip_id}}));
    }

    QtApiResult QtTripClient::getEventsSince(const QString &token, const QString &trip_id, quint64 since_revision)
    {
        return get(
            QStringLiteral("/events/since"),
            toPairs({
                {"token", token},
                {"trip_id", trip_id},
                {"since_revision", QString::number(since_revision)}}));
    }

    QtApiResult QtTripClient::addDay(const QString &token, const QString &trip_id, quint64 expected_revision, const QString &day_name)
    {
        return postForm(
            QStringLiteral("/days/add"),
            toPairs({
                {"token", token},
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"day_name", day_name}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"day_id", day_id},
                {"new_name", new_name}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"day_id", day_id}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"day_ids_order", joinCsv(day_ids_order)}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"day_id", day_id},
                {"expected_revision", QString::number(expected_revision)},
                {"name", name},
                {"time", time},
                {"notes", notes},
                {"category", category},
                {"link", link}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"day_id", day_id},
                {"expected_revision", QString::number(expected_revision)},
                {"item_id", item_id},
                {"name", name},
                {"time", time},
                {"notes", notes},
                {"category", category},
                {"link", link}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"day_id", day_id},
                {"expected_revision", QString::number(expected_revision)},
                {"item_id", item_id}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"day_id", day_id},
                {"expected_revision", QString::number(expected_revision)},
                {"item_ids_order", joinCsv(item_ids_order)}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"text", text},
                {"assignee_user_id", assignee_user_id},
                {"deadline", deadline}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"task_id", task_id},
                {"text", text},
                {"done", done ? QStringLiteral("true") : QStringLiteral("false")},
                {"assignee_user_id", assignee_user_id},
                {"deadline", deadline}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"task_id", task_id},
                {"done", done ? QStringLiteral("true") : QStringLiteral("false")}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"task_id", task_id}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"currency", currency},
                {"total_limit", QString::number(total_limit, 'f', 2)}}));
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
                {"token", token},
                {"trip_id", trip_id},
                {"expected_revision", QString::number(expected_revision)},
                {"amount", QString::number(amount, 'f', 2)},
                {"category", category},
                {"paid_by_user_id", paid_by_user_id},
                {"comment", comment},
                {"date", date},
                {"day_id", day_id}}));
    }

    QtApiResult QtTripClient::getBudgetSummary(const QString &token, const QString &trip_id)
    {
        return get(QStringLiteral("/budget/summary"), toPairs({{"token", token}, {"trip_id", trip_id}}));
    }

    QtApiResult QtTripClient::sendChatMessage(const QString &token, const QString &trip_id, const QString &text)
    {
        return postForm(
            QStringLiteral("/chat/send"),
            toPairs({{"token", token}, {"trip_id", trip_id}, {"text", text}}));
    }

    QtApiResult QtTripClient::listChatMessages(const QString &token, const QString &trip_id)
    {
        return get(QStringLiteral("/chat/list"), toPairs({{"token", token}, {"trip_id", trip_id}}));
    }

    QtApiResult QtTripClient::searchInTrip(const QString &token, const QString &trip_id, const QString &query)
    {
        return get(
            QStringLiteral("/search"),
            toPairs({{"token", token}, {"trip_id", trip_id}, {"query", query}}));
    }

    QtApiResult QtTripClient::exportTripJson(const QString &token, const QString &trip_id)
    {
        return get(QStringLiteral("/trips/export_json"), toPairs({{"token", token}, {"trip_id", trip_id}}));
    }

    QtApiResult QtTripClient::importTripJson(const QString &token, const QString &trip_json)
    {
        return postForm(
            QStringLiteral("/trips/import_json"),
            toPairs({{"token", token}, {"trip_json", trip_json}}));
    }

    QUrl QtTripClient::updatesWebSocketUrl(
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
        query.addQueryItem(QStringLiteral("token"), token);
        query.addQueryItem(QStringLiteral("trip_id"), trip_id);
        query.addQueryItem(QStringLiteral("since_revision"), QString::number(since_revision));
        url.setQuery(query);
        return url;
    }

    QtApiResult QtTripClient::get(const QString &path, const QList<QPair<QString, QString>> &query_items)
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
        return runRequest(std::move(request));
    }

    QtApiResult QtTripClient::postForm(const QString &path, const QList<QPair<QString, QString>> &form_items)
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

        return runRequest(std::move(request), form.toString(QUrl::FullyEncoded).toUtf8());
    }

    QtApiResult QtTripClient::runRequest(QNetworkRequest request, const QByteArray &body)
    {
        QNetworkReply *reply = body.isEmpty() ? network_.get(request) : network_.post(request, body);

        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        QtApiResult result;
        result.http_status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

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

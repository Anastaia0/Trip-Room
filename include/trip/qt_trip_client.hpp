#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace trip
{
    struct QtApiResult
    {
        bool ok = false;
        int http_status = 0;
        QString status;
        QString message;
        QJsonObject payload;
    };

    class QtTripClient
    {
    public:
        explicit QtTripClient(QString base_url = QStringLiteral("http://127.0.0.1:8080"));

        void setBaseUrl(const QString &base_url);
        [[nodiscard]] QString baseUrl() const;

        QtApiResult health();
        QtApiResult registerUser(const QString &login, const QString &password);
        QtApiResult login(const QString &login, const QString &password);

        QtApiResult listTrips(const QString &token);
        QtApiResult createTrip(
            const QString &token,
            const QString &title,
            const QString &start_date,
            const QString &end_date,
            const QString &description);
        QtApiResult deleteTrip(const QString &token, const QString &trip_id);
        QtApiResult updateTripInfo(
            const QString &token,
            const QString &trip_id,
            quint64 expected_revision,
            const QString &title,
            const QString &start_date,
            const QString &end_date,
            const QString &description);

        QtApiResult createInvite(const QString &token, const QString &trip_id, const QString &role);
        QtApiResult acceptInvite(const QString &token, const QString &invite_code);
        QtApiResult changeMemberRole(
            const QString &token,
            const QString &trip_id,
            const QString &target_user_id,
            const QString &new_role);
        QtApiResult removeMember(const QString &token, const QString &trip_id, const QString &target_user_id);

        QtApiResult getRevision(const QString &token, const QString &trip_id);
        QtApiResult getSnapshot(const QString &token, const QString &trip_id);
        QtApiResult getEventsSince(const QString &token, const QString &trip_id, quint64 since_revision);

        QtApiResult addDay(const QString &token, const QString &trip_id, quint64 expected_revision, const QString &day_name);
        QtApiResult renameDay(
            const QString &token,
            const QString &trip_id,
            quint64 expected_revision,
            const QString &day_id,
            const QString &new_name);
        QtApiResult removeDay(
            const QString &token,
            const QString &trip_id,
            quint64 expected_revision,
            const QString &day_id);
        QtApiResult reorderDays(
            const QString &token,
            const QString &trip_id,
            quint64 expected_revision,
            const QStringList &day_ids_order);

        QtApiResult addPlanItem(
            const QString &token,
            const QString &trip_id,
            const QString &day_id,
            quint64 expected_revision,
            const QString &name,
            const QString &time,
            const QString &notes,
            const QString &category,
            const QString &link);
        QtApiResult updatePlanItem(
            const QString &token,
            const QString &trip_id,
            const QString &day_id,
            quint64 expected_revision,
            const QString &item_id,
            const QString &name,
            const QString &time,
            const QString &notes,
            const QString &category,
            const QString &link);
        QtApiResult removePlanItem(
            const QString &token,
            const QString &trip_id,
            const QString &day_id,
            quint64 expected_revision,
            const QString &item_id);
        QtApiResult reorderPlanItems(
            const QString &token,
            const QString &trip_id,
            const QString &day_id,
            quint64 expected_revision,
            const QStringList &item_ids_order);

        QtApiResult addTask(
            const QString &token,
            const QString &trip_id,
            quint64 expected_revision,
            const QString &text,
            const QString &assignee_user_id = QString(),
            const QString &deadline = QString());
        QtApiResult updateTask(
            const QString &token,
            const QString &trip_id,
            quint64 expected_revision,
            const QString &task_id,
            const QString &text,
            bool done,
            const QString &assignee_user_id = QString(),
            const QString &deadline = QString());
        QtApiResult setTaskDone(
            const QString &token,
            const QString &trip_id,
            quint64 expected_revision,
            const QString &task_id,
            bool done);
        QtApiResult removeTask(
            const QString &token,
            const QString &trip_id,
            quint64 expected_revision,
            const QString &task_id);

        QtApiResult setBudgetSettings(
            const QString &token,
            const QString &trip_id,
            quint64 expected_revision,
            const QString &currency,
            double total_limit);
        QtApiResult addExpense(
            const QString &token,
            const QString &trip_id,
            quint64 expected_revision,
            double amount,
            const QString &category,
            const QString &paid_by_user_id,
            const QString &comment,
            const QString &date,
            const QString &day_id = QString());
        QtApiResult getBudgetSummary(const QString &token, const QString &trip_id);

        QtApiResult sendChatMessage(const QString &token, const QString &trip_id, const QString &text);
        QtApiResult listChatMessages(const QString &token, const QString &trip_id);

        QtApiResult searchInTrip(const QString &token, const QString &trip_id, const QString &query);
        QtApiResult exportTripJson(const QString &token, const QString &trip_id);
        QtApiResult importTripJson(const QString &token, const QString &trip_json);

        [[nodiscard]] QNetworkRequest updatesWebSocketRequest(
            const QString &token,
            const QString &trip_id,
            quint64 since_revision) const;

    private:
        QtApiResult get(const QString &path, const QList<QPair<QString, QString>> &query_items = {}, const QString &token = QString());
        QtApiResult postForm(const QString &path, const QList<QPair<QString, QString>> &form_items);
        QtApiResult runRequest(QNetworkRequest request, const QByteArray &body = QByteArray());
        [[nodiscard]] QUrl makeUrl(const QString &path) const;

        QString base_url_;
        QNetworkAccessManager network_;
    };
}

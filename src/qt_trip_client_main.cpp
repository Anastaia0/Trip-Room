#include "../include/trip/qt_trip_client.hpp"

#include <QtWebSockets/QWebSocket>

#include <QApplication>
#include <QAbstractSocket>
#include <QCheckBox>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDateEdit>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMetaObject>
#include <QObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <utility>

namespace
{
    QString defaultServerUrl()
    {
        return QStringLiteral("http://127.0.0.1:8080");
    }

    class TripClientWindow : public QWidget
    {
    public:
        explicit TripClientWindow(QString server_url = {})
            : client_(server_url.isEmpty() ? defaultServerUrl() : std::move(server_url)),
              network_client_(client_.baseUrl())
        {
            network_thread_.setObjectName(QStringLiteral("trip-network-thread"));
            network_context_.moveToThread(&network_thread_);
            network_thread_.start();

            buildUi();
            applyUiPolish();
            createNetworkSocket();
            wireUi();
            reconnect_timer_.setInterval(2000);
            reconnect_timer_.setSingleShot(true);
            reload_debounce_timer_.setInterval(100);
            reload_debounce_timer_.setSingleShot(true);
            appendLog(QStringLiteral("Qt client is ready"));
            appendLog(QStringLiteral("UI/render thread is active; HTTP and live sync use one dedicated connection thread"));
        }

        ~TripClientWindow() override
        {
            destroyNetworkSocket();
            network_thread_.quit();
            network_thread_.wait();
        }

    private:
        using ApiTask = std::function<trip::QtApiResult(trip::QtTripClient &)>;
        using ApiHandler = std::function<void(const trip::QtApiResult &)>;
        using LoadHandler = std::function<void()>;

        void buildUi()
        {
            setWindowTitle(QStringLiteral("Trip Planner Qt Client"));
            const QRect available_geometry = QGuiApplication::primaryScreen() != nullptr
                                                 ? QGuiApplication::primaryScreen()->availableGeometry()
                                                 : QRect(0, 0, 1280, 800);
            const int window_width = std::max(760, std::min(1400, available_geometry.width() - 80));
            const int window_height = std::max(560, std::min(820, available_geometry.height() - 100));
            resize(window_width, window_height);
            setMinimumSize(760, 560);

            auto *root_layout = new QVBoxLayout(this);
            root_layout->setContentsMargins(14, 14, 14, 14);
            root_layout->setSpacing(10);

            auto *splitter = new QSplitter();
            splitter->setChildrenCollapsible(false);
            root_layout->addWidget(splitter, 1);

            auto *sidebar = new QWidget();
            auto *sidebar_layout = new QVBoxLayout(sidebar);
            sidebar_layout->setContentsMargins(12, 8, 12, 12);
            sidebar_layout->setSpacing(10);

            auto *server_group = new QGroupBox(QStringLiteral("Session"));
            auto *server_layout = new QFormLayout(server_group);
            base_url_edit_ = new QLineEdit(client_.baseUrl());
            login_edit_ = new QLineEdit(QStringLiteral("owner_qt"));
            password_edit_ = new QLineEdit(QStringLiteral("pass"));
            password_edit_->setEchoMode(QLineEdit::Password);
            health_button_ = new QPushButton(QStringLiteral("Health"));
            register_button_ = new QPushButton(QStringLiteral("Register"));
            login_button_ = new QPushButton(QStringLiteral("Login"));
            session_label_ = new QLabel(QStringLiteral("User: <not logged in>"));
            socket_label_ = new QLabel(QStringLiteral("Live sync: disconnected"));
            network_label_ = new QLabel(QStringLiteral("Network: idle"));
            network_label_->setObjectName(QStringLiteral("networkStatus"));

            auto *auth_buttons = new QWidget();
            auto *auth_buttons_layout = new QHBoxLayout(auth_buttons);
            auth_buttons_layout->setContentsMargins(0, 0, 0, 0);
            auth_buttons_layout->addWidget(register_button_);
            auth_buttons_layout->addWidget(login_button_);

            server_layout->addRow(QStringLiteral("Base URL"), base_url_edit_);
            server_layout->addRow(QStringLiteral("Login"), login_edit_);
            server_layout->addRow(QStringLiteral("Password"), password_edit_);
            server_layout->addRow(QString(), health_button_);
            server_layout->addRow(QString(), auth_buttons);
            server_layout->addRow(QString(), session_label_);
            server_layout->addRow(QString(), network_label_);
            server_layout->addRow(QString(), socket_label_);

            auto *trips_group = new QGroupBox(QStringLiteral("Trips"));
            auto *trips_layout = new QVBoxLayout(trips_group);
            refresh_trips_button_ = new QPushButton(QStringLiteral("Refresh Trips"));
            trips_list_ = new QListWidget();
            connect_trip_button_ = new QPushButton(QStringLiteral("Load Selected Trip"));
            live_connect_button_ = new QPushButton(QStringLiteral("Connect Live"));
            live_disconnect_button_ = new QPushButton(QStringLiteral("Disconnect Live"));
            trips_layout->addWidget(refresh_trips_button_);
            trips_layout->addWidget(trips_list_, 1);
            trips_layout->addWidget(connect_trip_button_);
            trips_layout->addWidget(live_connect_button_);
            trips_layout->addWidget(live_disconnect_button_);

            auto *actions_group = new QGroupBox(QStringLiteral("Files"));
            auto *actions_layout = new QVBoxLayout(actions_group);
            export_button_ = new QPushButton(QStringLiteral("Export JSON"));
            import_button_ = new QPushButton(QStringLiteral("Import JSON"));
            fetch_events_button_ = new QPushButton(QStringLiteral("Fetch Events"));
            actions_layout->addWidget(export_button_);
            actions_layout->addWidget(import_button_);
            actions_layout->addWidget(fetch_events_button_);

            sidebar_layout->addWidget(server_group);
            sidebar_layout->addWidget(trips_group, 1);
            sidebar_layout->addWidget(actions_group);

            auto *main_area = new QWidget();
            auto *main_layout = new QVBoxLayout(main_area);
            main_layout->setContentsMargins(12, 8, 12, 12);
            main_layout->setSpacing(10);

            auto *trip_info_group = new QGroupBox(QStringLiteral("Current Trip"));
            auto *trip_info_layout = new QFormLayout(trip_info_group);
            title_edit_ = new QLineEdit(QStringLiteral("Qt Demo Trip"));
            start_date_edit_ = new QDateEdit();
            start_date_edit_->setCalendarPopup(true);
            start_date_edit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
            start_date_edit_->setDate(QDate::fromString(QStringLiteral("2026-08-01"), QStringLiteral("yyyy-MM-dd")));
            end_date_edit_ = new QDateEdit();
            end_date_edit_->setCalendarPopup(true);
            end_date_edit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
            end_date_edit_->setDate(QDate::fromString(QStringLiteral("2026-08-07"), QStringLiteral("yyyy-MM-dd")));
            description_edit_ = new QTextEdit();
            description_edit_->setMaximumHeight(80);
            create_trip_button_ = new QPushButton(QStringLiteral("Create Trip"));
            update_trip_button_ = new QPushButton(QStringLiteral("Update Trip"));
            delete_trip_button_ = new QPushButton(QStringLiteral("Delete Trip"));
            refresh_trip_button_ = new QPushButton(QStringLiteral("Refresh Snapshot"));
            trip_id_label_ = new QLabel(QStringLiteral("Trip ID: <none>"));
            revision_label_ = new QLabel(QStringLiteral("Revision: 0"));
            counts_label_ = new QLabel(QStringLiteral("Days: 0 | Tasks: 0 | Expenses: 0 | Members: 0"));

            auto *trip_buttons = new QWidget();
            auto *trip_buttons_layout = new QHBoxLayout(trip_buttons);
            trip_buttons_layout->setContentsMargins(0, 0, 0, 0);
            trip_buttons_layout->addWidget(create_trip_button_);
            trip_buttons_layout->addWidget(update_trip_button_);
            trip_buttons_layout->addWidget(delete_trip_button_);
            trip_buttons_layout->addWidget(refresh_trip_button_);

            trip_info_layout->addRow(QStringLiteral("Title"), title_edit_);
            trip_info_layout->addRow(QStringLiteral("Start"), start_date_edit_);
            trip_info_layout->addRow(QStringLiteral("End"), end_date_edit_);
            trip_info_layout->addRow(QStringLiteral("Description"), description_edit_);
            trip_info_layout->addRow(QString(), trip_buttons);
            trip_info_layout->addRow(QString(), trip_id_label_);
            trip_info_layout->addRow(QString(), revision_label_);
            trip_info_layout->addRow(QString(), counts_label_);

            tabs_ = new QTabWidget();
            buildOverviewTab();
            buildItineraryTab();
            buildTasksTab();
            buildBudgetTab();
            buildChatSearchTab();
            buildEventsTab();

            log_text_ = new QPlainTextEdit();
            log_text_->setReadOnly(true);
            log_text_->setMaximumBlockCount(500);
            log_text_->setPlaceholderText(QStringLiteral("Operation log"));

            main_layout->addWidget(trip_info_group);
            main_layout->addWidget(tabs_, 1);
            main_layout->addWidget(log_text_, 1);

            auto *sidebar_scroll = new QScrollArea();
            sidebar_scroll->setFrameShape(QFrame::NoFrame);
            sidebar_scroll->setWidgetResizable(true);
            sidebar_scroll->setWidget(sidebar);
            sidebar_scroll->setMinimumWidth(330);

            auto *main_scroll = new QScrollArea();
            main_scroll->setFrameShape(QFrame::NoFrame);
            main_scroll->setWidgetResizable(true);
            main_scroll->setWidget(main_area);

            splitter->addWidget(sidebar_scroll);
            splitter->addWidget(main_scroll);
            splitter->setStretchFactor(0, 0);
            splitter->setStretchFactor(1, 1);
            splitter->setSizes({330, std::max(430, window_width - 380)});
        }

        void buildOverviewTab()
        {
            auto *tab = new QWidget();
            auto *layout = new QHBoxLayout(tab);

            auto *invite_group = new QGroupBox(QStringLiteral("Invites"));
            auto *invite_layout = new QFormLayout(invite_group);
            invite_role_combo_ = new QComboBox();
            invite_role_combo_->addItems({QStringLiteral("Viewer"), QStringLiteral("Editor")});
            create_invite_button_ = new QPushButton(QStringLiteral("Create Invite"));
            invite_code_edit_ = new QLineEdit();
            invite_code_edit_->setPlaceholderText(QStringLiteral("Invite code"));
            accept_invite_button_ = new QPushButton(QStringLiteral("Accept Invite"));
            invite_output_edit_ = new QLineEdit();
            invite_output_edit_->setReadOnly(true);
            invite_layout->addRow(QStringLiteral("Role"), invite_role_combo_);
            invite_layout->addRow(QString(), create_invite_button_);
            invite_layout->addRow(QStringLiteral("Invite code"), invite_code_edit_);
            invite_layout->addRow(QString(), accept_invite_button_);
            invite_layout->addRow(QStringLiteral("Last invite"), invite_output_edit_);

            auto *members_group = new QGroupBox(QStringLiteral("Members"));
            auto *members_layout = new QVBoxLayout(members_group);
            members_table_ = new QTableWidget(0, 2);
            members_table_->setHorizontalHeaderLabels({QStringLiteral("User ID"), QStringLiteral("Role")});
            members_table_->horizontalHeader()->setStretchLastSection(true);
            members_table_->verticalHeader()->setVisible(false);
            members_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
            members_table_->setSelectionMode(QAbstractItemView::SingleSelection);

            auto *member_controls = new QWidget();
            auto *member_controls_layout = new QHBoxLayout(member_controls);
            member_controls_layout->setContentsMargins(0, 0, 0, 0);
            member_role_combo_ = new QComboBox();
            member_role_combo_->addItems({QStringLiteral("Viewer"), QStringLiteral("Editor")});
            change_member_role_button_ = new QPushButton(QStringLiteral("Change Role"));
            remove_member_button_ = new QPushButton(QStringLiteral("Remove"));
            member_controls_layout->addWidget(member_role_combo_);
            member_controls_layout->addWidget(change_member_role_button_);
            member_controls_layout->addWidget(remove_member_button_);

            members_layout->addWidget(members_table_, 1);
            members_layout->addWidget(member_controls);

            layout->addWidget(invite_group);
            layout->addWidget(members_group, 1);
            tabs_->addTab(tab, QStringLiteral("Overview"));
        }

        void buildItineraryTab()
        {
            auto *tab = new QWidget();
            auto *layout = new QHBoxLayout(tab);

            auto *days_group = new QGroupBox(QStringLiteral("Days"));
            auto *days_layout = new QVBoxLayout(days_group);
            days_table_ = new QTableWidget(0, 2);
            days_table_->setHorizontalHeaderLabels({QStringLiteral("Day ID"), QStringLiteral("Name")});
            days_table_->horizontalHeader()->setStretchLastSection(true);
            days_table_->verticalHeader()->setVisible(false);
            days_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
            day_name_edit_ = new QLineEdit();
            day_name_edit_->setPlaceholderText(QStringLiteral("Day name"));
            auto *day_buttons = new QWidget();
            auto *day_buttons_layout = new QHBoxLayout(day_buttons);
            day_buttons_layout->setContentsMargins(0, 0, 0, 0);
            add_day_button_ = new QPushButton(QStringLiteral("Add"));
            rename_day_button_ = new QPushButton(QStringLiteral("Rename"));
            remove_day_button_ = new QPushButton(QStringLiteral("Remove"));
            move_day_up_button_ = new QPushButton(QStringLiteral("Move Up"));
            move_day_down_button_ = new QPushButton(QStringLiteral("Move Down"));
            day_buttons_layout->addWidget(add_day_button_);
            day_buttons_layout->addWidget(rename_day_button_);
            day_buttons_layout->addWidget(remove_day_button_);
            day_buttons_layout->addWidget(move_day_up_button_);
            day_buttons_layout->addWidget(move_day_down_button_);
            days_layout->addWidget(days_table_, 1);
            days_layout->addWidget(day_name_edit_);
            days_layout->addWidget(day_buttons);

            auto *plan_group = new QGroupBox(QStringLiteral("Plan Items"));
            auto *plan_layout = new QVBoxLayout(plan_group);
            selected_day_label_ = new QLabel(QStringLiteral("Selected day: <none>"));
            plan_table_ = new QTableWidget(0, 5);
            plan_table_->setHorizontalHeaderLabels(
                {QStringLiteral("Name"), QStringLiteral("Time"), QStringLiteral("Category"), QStringLiteral("Link"), QStringLiteral("Notes")});
            plan_table_->horizontalHeader()->setStretchLastSection(true);
            plan_table_->verticalHeader()->setVisible(false);
            plan_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
            plan_name_edit_ = new QLineEdit();
            plan_time_edit_ = new QLineEdit();
            plan_category_edit_ = new QLineEdit();
            plan_link_edit_ = new QLineEdit();
            plan_notes_edit_ = new QTextEdit();
            plan_notes_edit_->setMaximumHeight(70);
            auto *plan_form = new QFormLayout();
            plan_form->addRow(QStringLiteral("Name"), plan_name_edit_);
            plan_form->addRow(QStringLiteral("Time"), plan_time_edit_);
            plan_form->addRow(QStringLiteral("Category"), plan_category_edit_);
            plan_form->addRow(QStringLiteral("Link"), plan_link_edit_);
            plan_form->addRow(QStringLiteral("Notes"), plan_notes_edit_);
            auto *plan_buttons = new QWidget();
            auto *plan_buttons_layout = new QHBoxLayout(plan_buttons);
            plan_buttons_layout->setContentsMargins(0, 0, 0, 0);
            add_plan_button_ = new QPushButton(QStringLiteral("Add"));
            update_plan_button_ = new QPushButton(QStringLiteral("Update"));
            remove_plan_button_ = new QPushButton(QStringLiteral("Remove"));
            move_plan_up_button_ = new QPushButton(QStringLiteral("Move Up"));
            move_plan_down_button_ = new QPushButton(QStringLiteral("Move Down"));
            plan_buttons_layout->addWidget(add_plan_button_);
            plan_buttons_layout->addWidget(update_plan_button_);
            plan_buttons_layout->addWidget(remove_plan_button_);
            plan_buttons_layout->addWidget(move_plan_up_button_);
            plan_buttons_layout->addWidget(move_plan_down_button_);
            plan_layout->addWidget(selected_day_label_);
            plan_layout->addWidget(plan_table_, 1);
            plan_layout->addLayout(plan_form);
            plan_layout->addWidget(plan_buttons);

            layout->addWidget(days_group, 1);
            layout->addWidget(plan_group, 2);
            tabs_->addTab(tab, QStringLiteral("Itinerary"));
        }

        void buildTasksTab()
        {
            auto *tab = new QWidget();
            auto *layout = new QVBoxLayout(tab);

            tasks_table_ = new QTableWidget(0, 4);
            tasks_table_->setHorizontalHeaderLabels(
                {QStringLiteral("Text"), QStringLiteral("Done"), QStringLiteral("Assignee"), QStringLiteral("Deadline")});
            tasks_table_->horizontalHeader()->setStretchLastSection(true);
            tasks_table_->verticalHeader()->setVisible(false);
            tasks_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
            tasks_table_->setSelectionMode(QAbstractItemView::SingleSelection);
            tasks_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

            task_text_edit_ = new QLineEdit();
            task_assignee_combo_ = new QComboBox();
            task_deadline_edit_ = new QLineEdit();
            task_deadline_edit_->setPlaceholderText(QStringLiteral("yyyy-MM-dd"));
            task_done_check_ = new QCheckBox(QStringLiteral("Done"));

            auto *task_form = new QFormLayout();
            task_form->addRow(QStringLiteral("Text"), task_text_edit_);
            task_form->addRow(QStringLiteral("Assignee"), task_assignee_combo_);
            task_form->addRow(QStringLiteral("Deadline"), task_deadline_edit_);
            task_form->addRow(QString(), task_done_check_);

            auto *task_buttons = new QWidget();
            auto *task_buttons_layout = new QHBoxLayout(task_buttons);
            task_buttons_layout->setContentsMargins(0, 0, 0, 0);
            add_task_button_ = new QPushButton(QStringLiteral("Add"));
            update_task_button_ = new QPushButton(QStringLiteral("Update"));
            toggle_task_button_ = new QPushButton(QStringLiteral("Apply Done"));
            remove_task_button_ = new QPushButton(QStringLiteral("Remove"));
            task_buttons_layout->addWidget(add_task_button_);
            task_buttons_layout->addWidget(update_task_button_);
            task_buttons_layout->addWidget(toggle_task_button_);
            task_buttons_layout->addWidget(remove_task_button_);

            layout->addWidget(tasks_table_, 1);
            layout->addLayout(task_form);
            layout->addWidget(task_buttons);
            tabs_->addTab(tab, QStringLiteral("Tasks"));
        }

        void buildBudgetTab()
        {
            auto *tab = new QWidget();
            auto *layout = new QHBoxLayout(tab);

            auto *settings_group = new QGroupBox(QStringLiteral("Budget"));
            auto *settings_layout = new QFormLayout(settings_group);
            budget_currency_edit_ = new QLineEdit(QStringLiteral("EUR"));
            budget_limit_edit_ = new QLineEdit(QStringLiteral("1000"));
            save_budget_button_ = new QPushButton(QStringLiteral("Save Budget"));
            settings_layout->addRow(QStringLiteral("Currency"), budget_currency_edit_);
            settings_layout->addRow(QStringLiteral("Limit"), budget_limit_edit_);
            settings_layout->addRow(QString(), save_budget_button_);

            auto *expenses_group = new QGroupBox(QStringLiteral("Expenses"));
            auto *expenses_layout = new QVBoxLayout(expenses_group);
            expenses_table_ = new QTableWidget(0, 5);
            expenses_table_->setHorizontalHeaderLabels(
                {QStringLiteral("Amount"), QStringLiteral("Category"), QStringLiteral("Paid By"), QStringLiteral("Date"), QStringLiteral("Comment")});
            expenses_table_->horizontalHeader()->setStretchLastSection(true);
            expenses_table_->verticalHeader()->setVisible(false);
            expense_amount_edit_ = new QLineEdit(QStringLiteral("0"));
            expense_category_edit_ = new QLineEdit();
            expense_payer_combo_ = new QComboBox();
            expense_date_edit_ = new QLineEdit(QStringLiteral("2026-08-01"));
            expense_day_combo_ = new QComboBox();
            expense_comment_edit_ = new QLineEdit();
            auto *expense_form = new QFormLayout();
            expense_form->addRow(QStringLiteral("Amount"), expense_amount_edit_);
            expense_form->addRow(QStringLiteral("Category"), expense_category_edit_);
            expense_form->addRow(QStringLiteral("Paid By"), expense_payer_combo_);
            expense_form->addRow(QStringLiteral("Date"), expense_date_edit_);
            expense_form->addRow(QStringLiteral("Day"), expense_day_combo_);
            expense_form->addRow(QStringLiteral("Comment"), expense_comment_edit_);
            add_expense_button_ = new QPushButton(QStringLiteral("Add Expense"));
            expenses_layout->addWidget(expenses_table_, 1);
            expenses_layout->addLayout(expense_form);
            expenses_layout->addWidget(add_expense_button_);

            auto *summary_group = new QGroupBox(QStringLiteral("Summary"));
            auto *summary_layout = new QVBoxLayout(summary_group);
            budget_summary_text_ = new QPlainTextEdit();
            budget_summary_text_->setReadOnly(true);
            summary_layout->addWidget(budget_summary_text_);

            auto *right = new QWidget();
            auto *right_layout = new QVBoxLayout(right);
            right_layout->addWidget(expenses_group, 2);
            right_layout->addWidget(summary_group, 1);

            layout->addWidget(settings_group);
            layout->addWidget(right, 1);
            tabs_->addTab(tab, QStringLiteral("Budget"));
        }

        void buildChatSearchTab()
        {
            auto *tab = new QWidget();
            auto *layout = new QHBoxLayout(tab);

            auto *chat_group = new QGroupBox(QStringLiteral("Chat"));
            auto *chat_layout = new QVBoxLayout(chat_group);
            chat_table_ = new QTableWidget(0, 3);
            chat_table_->setHorizontalHeaderLabels({QStringLiteral("User"), QStringLiteral("Message"), QStringLiteral("Time")});
            chat_table_->horizontalHeader()->setStretchLastSection(true);
            chat_table_->verticalHeader()->setVisible(false);
            chat_message_edit_ = new QLineEdit();
            send_chat_button_ = new QPushButton(QStringLiteral("Send"));
            chat_layout->addWidget(chat_table_, 1);
            chat_layout->addWidget(chat_message_edit_);
            chat_layout->addWidget(send_chat_button_);

            auto *search_group = new QGroupBox(QStringLiteral("Search"));
            auto *search_layout = new QVBoxLayout(search_group);
            search_query_edit_ = new QLineEdit();
            search_button_ = new QPushButton(QStringLiteral("Search"));
            search_results_table_ = new QTableWidget(0, 3);
            search_results_table_->setHorizontalHeaderLabels({QStringLiteral("Kind"), QStringLiteral("ID"), QStringLiteral("Text")});
            search_results_table_->horizontalHeader()->setStretchLastSection(true);
            search_results_table_->verticalHeader()->setVisible(false);
            search_layout->addWidget(search_query_edit_);
            search_layout->addWidget(search_button_);
            search_layout->addWidget(search_results_table_, 1);

            layout->addWidget(chat_group, 1);
            layout->addWidget(search_group, 1);
            tabs_->addTab(tab, QStringLiteral("Chat & Search"));
        }

        void buildEventsTab()
        {
            auto *tab = new QWidget();
            auto *layout = new QVBoxLayout(tab);

            events_table_ = new QTableWidget(0, 5);
            events_table_->setHorizontalHeaderLabels(
                {QStringLiteral("Rev"), QStringLiteral("Actor"), QStringLiteral("Action"), QStringLiteral("Entity"), QStringLiteral("Details")});
            events_table_->horizontalHeader()->setStretchLastSection(true);
            events_table_->verticalHeader()->setVisible(false);
            raw_json_text_ = new QPlainTextEdit();
            raw_json_text_->setReadOnly(true);
            raw_json_text_->setPlaceholderText(QStringLiteral("Exported JSON or latest raw payload"));
            layout->addWidget(events_table_, 2);
            layout->addWidget(raw_json_text_, 1);
            tabs_->addTab(tab, QStringLiteral("Events"));
        }

        void wireUi();
        void createNetworkSocket();
        void destroyNetworkSocket();
        void applyUiPolish();
        void appendLog(const QString &line);
        void appendResult(const QString &prefix, const trip::QtApiResult &result);
        void runNetworkOperation(const QString &prefix, ApiTask task, ApiHandler on_success = {}, ApiHandler on_failure = {});
        void runNetworkMutation(const QString &prefix, std::function<trip::QtApiResult(trip::QtTripClient &, quint64)> task, ApiHandler on_success = {});
        void setNetworkBusy(bool busy, const QString &operation = {});
        bool hasSession() const;
        bool hasCurrentTrip() const;
        void refreshTripsList(bool keep_selection = true, LoadHandler on_loaded = {});
        void selectTrip(const QString &trip_id);
        void loadCurrentTrip(LoadHandler on_loaded = {});
        void refreshBudgetSummary();
        void refreshMemberEditors();
        void populateTripsList(const QJsonArray &trips);
        void populateMembersTable(const QJsonObject &members);
        void populateDaysTable(const QJsonArray &days);
        void populatePlanTable(const QJsonObject &selected_day);
        void populateTasksTable(const QJsonArray &tasks);
        void populateExpensesTable(const QJsonArray &expenses);
        void populateChatTable(const QJsonArray &messages);
        void populateEventsTable(const QJsonArray &events);
        void populateSearchResults(const QJsonArray &hits);
        void populateRawJson(const QString &json_text);
        void selectTask(const QString &task_id);
        void clearTaskEditor();
        QString selectedTripId() const;
        QString selectedMemberId() const;
        QString selectedDayId() const;
        QString selectedPlanItemId() const;
        QString selectedTaskId() const;
        QJsonObject selectedDayObject() const;
        QStringList currentDayOrder() const;
        QStringList currentPlanOrder(const QJsonObject &day) const;
        void connectLiveUpdates();
        void disconnectLiveUpdates(bool manual_disconnect);
        void scheduleReconnect();
        void handleSocketMessage(const QString &payload);

        trip::QtTripClient client_;
        trip::QtTripClient network_client_;
        QObject network_context_;
        QThread network_thread_;
        QWebSocket *socket_ = nullptr;
        QTimer reconnect_timer_;
        QTimer reload_debounce_timer_;
        int pending_network_jobs_ = 0;
        bool live_sync_requested_ = false;
        bool live_socket_active_ = false;
        bool manual_socket_close_ = false;
        quint64 current_revision_ = 0;
        quint64 last_seen_revision_ = 0;
        QString token_;
        QString current_user_id_;
        QString current_trip_id_;
        QJsonObject current_trip_;

        QTabWidget *tabs_ = nullptr;
        QLineEdit *base_url_edit_ = nullptr;
        QLineEdit *login_edit_ = nullptr;
        QLineEdit *password_edit_ = nullptr;
        QPushButton *health_button_ = nullptr;
        QPushButton *register_button_ = nullptr;
        QPushButton *login_button_ = nullptr;
        QLabel *session_label_ = nullptr;
        QLabel *network_label_ = nullptr;
        QLabel *socket_label_ = nullptr;
        QPushButton *refresh_trips_button_ = nullptr;
        QListWidget *trips_list_ = nullptr;
        QPushButton *connect_trip_button_ = nullptr;
        QPushButton *live_connect_button_ = nullptr;
        QPushButton *live_disconnect_button_ = nullptr;
        QPushButton *export_button_ = nullptr;
        QPushButton *import_button_ = nullptr;
        QPushButton *fetch_events_button_ = nullptr;
        QLineEdit *title_edit_ = nullptr;
        QDateEdit *start_date_edit_ = nullptr;
        QDateEdit *end_date_edit_ = nullptr;
        QTextEdit *description_edit_ = nullptr;
        QPushButton *create_trip_button_ = nullptr;
        QPushButton *update_trip_button_ = nullptr;
        QPushButton *delete_trip_button_ = nullptr;
        QPushButton *refresh_trip_button_ = nullptr;
        QLabel *trip_id_label_ = nullptr;
        QLabel *revision_label_ = nullptr;
        QLabel *counts_label_ = nullptr;
        QComboBox *invite_role_combo_ = nullptr;
        QPushButton *create_invite_button_ = nullptr;
        QLineEdit *invite_code_edit_ = nullptr;
        QPushButton *accept_invite_button_ = nullptr;
        QLineEdit *invite_output_edit_ = nullptr;
        QTableWidget *members_table_ = nullptr;
        QComboBox *member_role_combo_ = nullptr;
        QPushButton *change_member_role_button_ = nullptr;
        QPushButton *remove_member_button_ = nullptr;
        QTableWidget *days_table_ = nullptr;
        QLineEdit *day_name_edit_ = nullptr;
        QPushButton *add_day_button_ = nullptr;
        QPushButton *rename_day_button_ = nullptr;
        QPushButton *remove_day_button_ = nullptr;
        QPushButton *move_day_up_button_ = nullptr;
        QPushButton *move_day_down_button_ = nullptr;
        QLabel *selected_day_label_ = nullptr;
        QTableWidget *plan_table_ = nullptr;
        QLineEdit *plan_name_edit_ = nullptr;
        QLineEdit *plan_time_edit_ = nullptr;
        QLineEdit *plan_category_edit_ = nullptr;
        QLineEdit *plan_link_edit_ = nullptr;
        QTextEdit *plan_notes_edit_ = nullptr;
        QPushButton *add_plan_button_ = nullptr;
        QPushButton *update_plan_button_ = nullptr;
        QPushButton *remove_plan_button_ = nullptr;
        QPushButton *move_plan_up_button_ = nullptr;
        QPushButton *move_plan_down_button_ = nullptr;
        QTableWidget *tasks_table_ = nullptr;
        QLineEdit *task_text_edit_ = nullptr;
        QComboBox *task_assignee_combo_ = nullptr;
        QLineEdit *task_deadline_edit_ = nullptr;
        QCheckBox *task_done_check_ = nullptr;
        QPushButton *add_task_button_ = nullptr;
        QPushButton *update_task_button_ = nullptr;
        QPushButton *toggle_task_button_ = nullptr;
        QPushButton *remove_task_button_ = nullptr;
        QLineEdit *budget_currency_edit_ = nullptr;
        QLineEdit *budget_limit_edit_ = nullptr;
        QPushButton *save_budget_button_ = nullptr;
        QTableWidget *expenses_table_ = nullptr;
        QLineEdit *expense_amount_edit_ = nullptr;
        QLineEdit *expense_category_edit_ = nullptr;
        QComboBox *expense_payer_combo_ = nullptr;
        QLineEdit *expense_date_edit_ = nullptr;
        QComboBox *expense_day_combo_ = nullptr;
        QLineEdit *expense_comment_edit_ = nullptr;
        QPushButton *add_expense_button_ = nullptr;
        QPlainTextEdit *budget_summary_text_ = nullptr;
        QTableWidget *chat_table_ = nullptr;
        QLineEdit *chat_message_edit_ = nullptr;
        QPushButton *send_chat_button_ = nullptr;
        QLineEdit *search_query_edit_ = nullptr;
        QPushButton *search_button_ = nullptr;
        QTableWidget *search_results_table_ = nullptr;
        QTableWidget *events_table_ = nullptr;
        QPlainTextEdit *raw_json_text_ = nullptr;
        QPlainTextEdit *log_text_ = nullptr;
    };

    void TripClientWindow::wireUi()
    {
        connect(&reconnect_timer_, &QTimer::timeout, this, [this]()
                { connectLiveUpdates(); });
        connect(&reload_debounce_timer_, &QTimer::timeout, this, [this]()
                {
            if (hasCurrentTrip())
            {
                loadCurrentTrip();
            } });

        connect(base_url_edit_, &QLineEdit::textChanged, this, [this](const QString &value)
                {
            client_.setBaseUrl(value);
            QSettings().setValue(QStringLiteral("server_url"), value);
            if (live_sync_requested_)
            {
                connectLiveUpdates();
            } });

        connect(health_button_, &QPushButton::clicked, this, [this]()
                {
            runNetworkOperation(
                QStringLiteral("Health"),
                [](trip::QtTripClient &client)
                {
                    return client.health();
                }); });

        connect(register_button_, &QPushButton::clicked, this, [this]()
                {
            const QString login = login_edit_->text();
            const QString password = password_edit_->text();
            runNetworkOperation(
                QStringLiteral("Register"),
                [login, password](trip::QtTripClient &client)
                {
                    return client.registerUser(login, password);
                },
                [this](const trip::QtApiResult &result)
                {
                    current_user_id_ = result.payload.value(QStringLiteral("user_id")).toString();
                }); });

        connect(login_button_, &QPushButton::clicked, this, [this]()
                {
            const QString login = login_edit_->text();
            const QString password = password_edit_->text();
            runNetworkOperation(
                QStringLiteral("Login"),
                [login, password](trip::QtTripClient &client)
                {
                    return client.login(login, password);
                },
                [this, login](const trip::QtApiResult &result)
                {
                    token_ = result.payload.value(QStringLiteral("token")).toString();
                    session_label_->setText(QStringLiteral("User: ") + login);
                    refreshTripsList(false);
                }); });

        connect(refresh_trips_button_, &QPushButton::clicked, this, [this]()
                { refreshTripsList(true); });

        connect(connect_trip_button_, &QPushButton::clicked, this, [this]()
                {
            current_trip_id_ = selectedTripId();
            loadCurrentTrip([this]()
                            {
                if (live_sync_requested_)
                {
                    connectLiveUpdates();
                } });
            });

        connect(trips_list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item)
                {
            if (item == nullptr)
            {
                return;
            }
            current_trip_id_ = item->data(Qt::UserRole).toString();
            loadCurrentTrip([this]()
                            {
                if (live_sync_requested_)
                {
                    connectLiveUpdates();
                } });
            });

        connect(create_trip_button_, &QPushButton::clicked, this, [this]()
                {
            if (!hasSession())
            {
                appendLog(QStringLiteral("Login first"));
                return;
            }
            const QString token = token_;
            const QString title = title_edit_->text();
            const QString start_date = start_date_edit_->date().toString(QStringLiteral("yyyy-MM-dd"));
            const QString end_date = end_date_edit_->date().toString(QStringLiteral("yyyy-MM-dd"));
            const QString description = description_edit_->toPlainText();
            runNetworkOperation(
                QStringLiteral("Create Trip"),
                [token, title, start_date, end_date, description](trip::QtTripClient &client)
                {
                    return client.createTrip(token, title, start_date, end_date, description);
                },
                [this](const trip::QtApiResult &result)
                {
                    current_trip_id_ = result.payload.value(QStringLiteral("trip_id")).toString();
                    refreshTripsList(false, [this]()
                                     {
                        selectTrip(current_trip_id_);
                        loadCurrentTrip([this]()
                                        {
                            if (live_sync_requested_)
                            {
                                connectLiveUpdates();
                            } });
                        });
                }); });

        connect(update_trip_button_, &QPushButton::clicked, this, [this]()
                {
            if (!hasCurrentTrip())
            {
                appendLog(QStringLiteral("Select a trip first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const QString title = title_edit_->text();
            const QString start_date = start_date_edit_->date().toString(QStringLiteral("yyyy-MM-dd"));
            const QString end_date = end_date_edit_->date().toString(QStringLiteral("yyyy-MM-dd"));
            const QString description = description_edit_->toPlainText();
            runNetworkMutation(
                QStringLiteral("Update Trip"),
                [token, trip_id, title, start_date, end_date, description](trip::QtTripClient &client, quint64 revision)
                {
                    return client.updateTripInfo(token, trip_id, revision, title, start_date, end_date, description);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                    refreshTripsList(true);
                }); });

        connect(delete_trip_button_, &QPushButton::clicked, this, [this]()
                {
            if (!hasCurrentTrip())
            {
                appendLog(QStringLiteral("Select a trip first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            runNetworkOperation(
                QStringLiteral("Delete Trip"),
                [token, trip_id](trip::QtTripClient &client)
                {
                    return client.deleteTrip(token, trip_id);
                },
                [this](const trip::QtApiResult &)
                {
                    disconnectLiveUpdates(true);
                    current_trip_id_.clear();
                    current_trip_ = {};
                    current_revision_ = 0;
                    last_seen_revision_ = 0;
                    refreshTripsList(false);
                    loadCurrentTrip();
                }); });

        connect(refresh_trip_button_, &QPushButton::clicked, this, [this]()
                { loadCurrentTrip(); });

        connect(create_invite_button_, &QPushButton::clicked, this, [this]()
                {
            if (!hasCurrentTrip())
            {
                appendLog(QStringLiteral("Select a trip first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const QString role = invite_role_combo_->currentText();
            runNetworkOperation(
                QStringLiteral("Create Invite"),
                [token, trip_id, role](trip::QtTripClient &client)
                {
                    return client.createInvite(token, trip_id, role);
                },
                [this](const trip::QtApiResult &result)
                {
                    invite_output_edit_->setText(result.payload.value(QStringLiteral("invite_code")).toString());
                }); });

        connect(accept_invite_button_, &QPushButton::clicked, this, [this]()
                {
            if (!hasSession())
            {
                appendLog(QStringLiteral("Login first"));
                return;
            }
            const QString token = token_;
            const QString invite_code = invite_code_edit_->text();
            runNetworkOperation(
                QStringLiteral("Accept Invite"),
                [token, invite_code](trip::QtTripClient &client)
                {
                    return client.acceptInvite(token, invite_code);
                },
                [this](const trip::QtApiResult &)
                {
                    refreshTripsList(false);
                }); });

        connect(change_member_role_button_, &QPushButton::clicked, this, [this]()
                {
            const QString member_id = selectedMemberId();
            if (member_id.isEmpty())
            {
                appendLog(QStringLiteral("Select a member first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const QString role = member_role_combo_->currentText();
            runNetworkOperation(
                QStringLiteral("Change Role"),
                [token, trip_id, member_id, role](trip::QtTripClient &client)
                {
                    return client.changeMemberRole(token, trip_id, member_id, role);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(remove_member_button_, &QPushButton::clicked, this, [this]()
                {
            const QString member_id = selectedMemberId();
            if (member_id.isEmpty())
            {
                appendLog(QStringLiteral("Select a member first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            runNetworkOperation(
                QStringLiteral("Remove Member"),
                [token, trip_id, member_id](trip::QtTripClient &client)
                {
                    return client.removeMember(token, trip_id, member_id);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(add_day_button_, &QPushButton::clicked, this, [this]()
                {
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const QString day_name = day_name_edit_->text();
            runNetworkMutation(
                QStringLiteral("Add Day"),
                [token, trip_id, day_name](trip::QtTripClient &client, quint64 revision)
                {
                    return client.addDay(token, trip_id, revision, day_name);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(rename_day_button_, &QPushButton::clicked, this, [this]()
                {
            const QString day_id = selectedDayId();
            if (day_id.isEmpty())
            {
                appendLog(QStringLiteral("Select a day first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const QString day_name = day_name_edit_->text();
            runNetworkMutation(
                QStringLiteral("Rename Day"),
                [token, trip_id, day_id, day_name](trip::QtTripClient &client, quint64 revision)
                {
                    return client.renameDay(token, trip_id, revision, day_id, day_name);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(remove_day_button_, &QPushButton::clicked, this, [this]()
                {
            const QString day_id = selectedDayId();
            if (day_id.isEmpty())
            {
                appendLog(QStringLiteral("Select a day first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            runNetworkMutation(
                QStringLiteral("Remove Day"),
                [token, trip_id, day_id](trip::QtTripClient &client, quint64 revision)
                {
                    return client.removeDay(token, trip_id, revision, day_id);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(move_day_up_button_, &QPushButton::clicked, this, [this]()
                {
            const int row = days_table_->currentRow();
            QStringList order = currentDayOrder();
            if (row <= 0 || row >= order.size())
            {
                appendLog(QStringLiteral("Select a movable day"));
                return;
            }
            order.swapItemsAt(row, row - 1);
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            runNetworkMutation(
                QStringLiteral("Move Day Up"),
                [token, trip_id, order](trip::QtTripClient &client, quint64 revision)
                {
                    return client.reorderDays(token, trip_id, revision, order);
                },
                [this, row](const trip::QtApiResult &)
                {
                    loadCurrentTrip([this, row]()
                                    {
                        if (row - 1 >= 0)
                        {
                            days_table_->selectRow(row - 1);
                        } });
                }); });

        connect(move_day_down_button_, &QPushButton::clicked, this, [this]()
                {
            const int row = days_table_->currentRow();
            QStringList order = currentDayOrder();
            if (row < 0 || row + 1 >= order.size())
            {
                appendLog(QStringLiteral("Select a movable day"));
                return;
            }
            order.swapItemsAt(row, row + 1);
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            runNetworkMutation(
                QStringLiteral("Move Day Down"),
                [token, trip_id, order](trip::QtTripClient &client, quint64 revision)
                {
                    return client.reorderDays(token, trip_id, revision, order);
                },
                [this, row](const trip::QtApiResult &)
                {
                    loadCurrentTrip([this, row]()
                                    {
                        days_table_->selectRow(row + 1);
                    });
                }); });

        connect(add_plan_button_, &QPushButton::clicked, this, [this]()
                {
            const QString day_id = selectedDayId();
            if (day_id.isEmpty())
            {
                appendLog(QStringLiteral("Select a day first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const QString name = plan_name_edit_->text();
            const QString time = plan_time_edit_->text();
            const QString notes = plan_notes_edit_->toPlainText();
            const QString category = plan_category_edit_->text();
            const QString link = plan_link_edit_->text();
            runNetworkMutation(
                QStringLiteral("Add Plan Item"),
                [token, trip_id, day_id, name, time, notes, category, link](trip::QtTripClient &client, quint64 revision)
                {
                    return client.addPlanItem(token, trip_id, day_id, revision, name, time, notes, category, link);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(update_plan_button_, &QPushButton::clicked, this, [this]()
                {
            const QString day_id = selectedDayId();
            const QString item_id = selectedPlanItemId();
            if (day_id.isEmpty() || item_id.isEmpty())
            {
                appendLog(QStringLiteral("Select a plan item first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const QString name = plan_name_edit_->text();
            const QString time = plan_time_edit_->text();
            const QString notes = plan_notes_edit_->toPlainText();
            const QString category = plan_category_edit_->text();
            const QString link = plan_link_edit_->text();
            runNetworkMutation(
                QStringLiteral("Update Plan Item"),
                [token, trip_id, day_id, item_id, name, time, notes, category, link](trip::QtTripClient &client, quint64 revision)
                {
                    return client.updatePlanItem(token, trip_id, day_id, revision, item_id, name, time, notes, category, link);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(remove_plan_button_, &QPushButton::clicked, this, [this]()
                {
            const QString day_id = selectedDayId();
            const QString item_id = selectedPlanItemId();
            if (day_id.isEmpty() || item_id.isEmpty())
            {
                appendLog(QStringLiteral("Select a plan item first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            runNetworkMutation(
                QStringLiteral("Remove Plan Item"),
                [token, trip_id, day_id, item_id](trip::QtTripClient &client, quint64 revision)
                {
                    return client.removePlanItem(token, trip_id, day_id, revision, item_id);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(move_plan_up_button_, &QPushButton::clicked, this, [this]()
                {
            const QJsonObject day = selectedDayObject();
            const int row = plan_table_->currentRow();
            QStringList order = currentPlanOrder(day);
            if (day.isEmpty() || row <= 0 || row >= order.size())
            {
                appendLog(QStringLiteral("Select a movable plan item"));
                return;
            }
            order.swapItemsAt(row, row - 1);
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const QString day_id = day.value(QStringLiteral("id")).toString();
            runNetworkMutation(
                QStringLiteral("Move Plan Item Up"),
                [token, trip_id, day_id, order](trip::QtTripClient &client, quint64 revision)
                {
                    return client.reorderPlanItems(token, trip_id, day_id, revision, order);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(move_plan_down_button_, &QPushButton::clicked, this, [this]()
                {
            const QJsonObject day = selectedDayObject();
            const int row = plan_table_->currentRow();
            QStringList order = currentPlanOrder(day);
            if (day.isEmpty() || row < 0 || row + 1 >= order.size())
            {
                appendLog(QStringLiteral("Select a movable plan item"));
                return;
            }
            order.swapItemsAt(row, row + 1);
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const QString day_id = day.value(QStringLiteral("id")).toString();
            runNetworkMutation(
                QStringLiteral("Move Plan Item Down"),
                [token, trip_id, day_id, order](trip::QtTripClient &client, quint64 revision)
                {
                    return client.reorderPlanItems(token, trip_id, day_id, revision, order);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(add_task_button_, &QPushButton::clicked, this, [this]()
                {
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const QString text = task_text_edit_->text();
            const QString assignee = task_assignee_combo_->currentData().toString();
            const QString deadline = task_deadline_edit_->text();
            runNetworkMutation(
                QStringLiteral("Add Task"),
                [token, trip_id, text, assignee, deadline](trip::QtTripClient &client, quint64 revision)
                {
                    return client.addTask(token, trip_id, revision, text, assignee, deadline);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(update_task_button_, &QPushButton::clicked, this, [this]()
                {
            const QString task_id = selectedTaskId();
            if (task_id.isEmpty())
            {
                appendLog(QStringLiteral("Select a task first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const QString text = task_text_edit_->text();
            const bool done = task_done_check_->isChecked();
            const QString assignee = task_assignee_combo_->currentData().toString();
            const QString deadline = task_deadline_edit_->text();
            runNetworkMutation(
                QStringLiteral("Update Task"),
                [token, trip_id, task_id, text, done, assignee, deadline](trip::QtTripClient &client, quint64 revision)
                {
                    return client.updateTask(token, trip_id, revision, task_id, text, done, assignee, deadline);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(toggle_task_button_, &QPushButton::clicked, this, [this]()
                {
            const QString task_id = selectedTaskId();
            if (task_id.isEmpty())
            {
                appendLog(QStringLiteral("Select a task first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const bool done = task_done_check_->isChecked();
            runNetworkMutation(
                QStringLiteral("Apply Task Done"),
                [token, trip_id, task_id, done](trip::QtTripClient &client, quint64 revision)
                {
                    return client.setTaskDone(token, trip_id, revision, task_id, done);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(remove_task_button_, &QPushButton::clicked, this, [this]()
                {
            const QString task_id = selectedTaskId();
            if (task_id.isEmpty())
            {
                appendLog(QStringLiteral("Select a task first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            runNetworkMutation(
                QStringLiteral("Remove Task"),
                [token, trip_id, task_id](trip::QtTripClient &client, quint64 revision)
                {
                    return client.removeTask(token, trip_id, revision, task_id);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(save_budget_button_, &QPushButton::clicked, this, [this]()
                {
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const QString currency = budget_currency_edit_->text();
            const double limit = budget_limit_edit_->text().toDouble();
            runNetworkMutation(
                QStringLiteral("Save Budget"),
                [token, trip_id, currency, limit](trip::QtTripClient &client, quint64 revision)
                {
                    return client.setBudgetSettings(token, trip_id, revision, currency, limit);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(add_expense_button_, &QPushButton::clicked, this, [this]()
                {
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const double amount = expense_amount_edit_->text().toDouble();
            const QString category = expense_category_edit_->text();
            const QString payer = expense_payer_combo_->currentData().toString();
            const QString comment = expense_comment_edit_->text();
            const QString date = expense_date_edit_->text();
            const QString day_id = expense_day_combo_->currentData().toString();
            runNetworkMutation(
                QStringLiteral("Add Expense"),
                [token, trip_id, amount, category, payer, comment, date, day_id](trip::QtTripClient &client, quint64 revision)
                {
                    return client.addExpense(token, trip_id, revision, amount, category, payer, comment, date, day_id);
                },
                [this](const trip::QtApiResult &)
                {
                    loadCurrentTrip();
                }); });

        connect(send_chat_button_, &QPushButton::clicked, this, [this]()
                {
            if (!hasCurrentTrip())
            {
                appendLog(QStringLiteral("Select a trip first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const QString text = chat_message_edit_->text();
            runNetworkOperation(
                QStringLiteral("Send Chat"),
                [token, trip_id, text](trip::QtTripClient &client)
                {
                    return client.sendChatMessage(token, trip_id, text);
                },
                [this](const trip::QtApiResult &)
                {
                    chat_message_edit_->clear();
                    loadCurrentTrip();
                }); });

        connect(search_button_, &QPushButton::clicked, this, [this]()
                {
            if (!hasCurrentTrip())
            {
                appendLog(QStringLiteral("Select a trip first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            const QString query = search_query_edit_->text();
            runNetworkOperation(
                QStringLiteral("Search"),
                [token, trip_id, query](trip::QtTripClient &client)
                {
                    return client.searchInTrip(token, trip_id, query);
                },
                [this](const trip::QtApiResult &result)
                {
                    populateSearchResults(result.payload.value(QStringLiteral("hits")).toArray());
                    populateRawJson(QString::fromUtf8(QJsonDocument(result.payload).toJson(QJsonDocument::Indented)));
                }); });

        connect(fetch_events_button_, &QPushButton::clicked, this, [this]()
                {
            if (!hasCurrentTrip())
            {
                appendLog(QStringLiteral("Select a trip first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            runNetworkOperation(
                QStringLiteral("Fetch Events"),
                [token, trip_id](trip::QtTripClient &client)
                {
                    return client.getEventsSince(token, trip_id, 0);
                },
                [this](const trip::QtApiResult &result)
                {
                    populateEventsTable(result.payload.value(QStringLiteral("events")).toArray());
                    populateRawJson(QString::fromUtf8(QJsonDocument(result.payload).toJson(QJsonDocument::Indented)));
                }); });

        connect(export_button_, &QPushButton::clicked, this, [this]()
                {
            if (!hasCurrentTrip())
            {
                appendLog(QStringLiteral("Select a trip first"));
                return;
            }
            const QString token = token_;
            const QString trip_id = current_trip_id_;
            runNetworkOperation(
                QStringLiteral("Export JSON"),
                [token, trip_id](trip::QtTripClient &client)
                {
                    return client.exportTripJson(token, trip_id);
                },
                [this, trip_id](const trip::QtApiResult &result)
                {
                    const QString json_text = result.payload.value(QStringLiteral("trip_json")).toString();
                    populateRawJson(json_text);
                    const QString file_path = QFileDialog::getSaveFileName(
                        this,
                        QStringLiteral("Export Trip JSON"),
                        trip_id + QStringLiteral(".json"),
                        QStringLiteral("JSON Files (*.json);;All Files (*)"));
                    if (file_path.isEmpty())
                    {
                        return;
                    }
                    QFile file(file_path);
                    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    {
                        file.write(json_text.toUtf8());
                        appendLog(QStringLiteral("Exported to ") + file_path);
                    }
                }); });

        connect(import_button_, &QPushButton::clicked, this, [this]()
                {
            if (!hasSession())
            {
                appendLog(QStringLiteral("Login first"));
                return;
            }
            const QString file_path = QFileDialog::getOpenFileName(
                this,
                QStringLiteral("Import Trip JSON"),
                QString(),
                QStringLiteral("JSON Files (*.json);;All Files (*)"));
            if (file_path.isEmpty())
            {
                return;
            }
            QFile file(file_path);
            if (!file.open(QIODevice::ReadOnly))
            {
                appendLog(QStringLiteral("Failed to open file: ") + file_path);
                return;
            }
            const QString json_text = QString::fromUtf8(file.readAll());
            const QString token = token_;
            runNetworkOperation(
                QStringLiteral("Import JSON"),
                [token, json_text](trip::QtTripClient &client)
                {
                    return client.importTripJson(token, json_text);
                },
                [this](const trip::QtApiResult &result)
                {
                    current_trip_id_ = result.payload.value(QStringLiteral("trip_id")).toString();
                    refreshTripsList(false, [this]()
                                     {
                        selectTrip(current_trip_id_);
                        loadCurrentTrip([this]()
                                        {
                            if (live_sync_requested_)
                            {
                                connectLiveUpdates();
                            } });
                        });
                }); });

        connect(live_connect_button_, &QPushButton::clicked, this, [this]()
                { connectLiveUpdates(); });

        connect(live_disconnect_button_, &QPushButton::clicked, this, [this]()
                { disconnectLiveUpdates(true); });

        connect(days_table_, &QTableWidget::itemSelectionChanged, this, [this]()
                {
            const QJsonObject day = selectedDayObject();
            day_name_edit_->setText(day.value(QStringLiteral("name")).toString());
            populatePlanTable(day); });

        connect(plan_table_, &QTableWidget::itemSelectionChanged, this, [this]()
                {
            const QJsonObject day = selectedDayObject();
            const QString item_id = selectedPlanItemId();
            for (const auto &value : day.value(QStringLiteral("items")).toArray())
            {
                const QJsonObject item = value.toObject();
                if (item.value(QStringLiteral("id")).toString() != item_id)
                {
                    continue;
                }
                plan_name_edit_->setText(item.value(QStringLiteral("name")).toString());
                plan_time_edit_->setText(item.value(QStringLiteral("time")).toString());
                plan_category_edit_->setText(item.value(QStringLiteral("category")).toString());
                plan_link_edit_->setText(item.value(QStringLiteral("link")).toString());
                plan_notes_edit_->setPlainText(item.value(QStringLiteral("notes")).toString());
                break;
            } });

        connect(tasks_table_, &QTableWidget::itemSelectionChanged, this, [this]()
                {
            const QString task_id = selectedTaskId();
            if (task_id.isEmpty())
            {
                clearTaskEditor();
                return;
            }
            bool found = false;
            for (const auto &value : current_trip_.value(QStringLiteral("tasks")).toArray())
            {
                const QJsonObject task = value.toObject();
                if (task.value(QStringLiteral("id")).toString() != task_id)
                {
                    continue;
                }
                task_text_edit_->setText(task.value(QStringLiteral("text")).toString());
                task_deadline_edit_->setText(task.value(QStringLiteral("deadline")).toString());
                task_done_check_->setChecked(task.value(QStringLiteral("done")).toBool());
                const QString assignee = task.value(QStringLiteral("assignee_user_id")).toString();
                const int combo_index = task_assignee_combo_->findData(assignee);
                if (combo_index >= 0)
                {
                    task_assignee_combo_->setCurrentIndex(combo_index);
                }
                found = true;
                break;
            }
            if (!found)
            {
                clearTaskEditor();
            } });

        connect(members_table_, &QTableWidget::itemSelectionChanged, this, [this]()
                {
            const int row = members_table_->currentRow();
            if (row < 0)
            {
                return;
            }
            const QString role = members_table_->item(row, 1)->text();
            const int combo_index = member_role_combo_->findText(role);
            if (combo_index >= 0)
            {
                member_role_combo_->setCurrentIndex(combo_index);
            } });
    }

    void TripClientWindow::createNetworkSocket()
    {
        const QPointer<TripClientWindow> self(this);
        QMetaObject::invokeMethod(
            &network_context_,
            [this, self]()
            {
                if (self.isNull())
                {
                    return;
                }

                socket_ = new QWebSocket();

                connect(socket_, &QWebSocket::connected, this, [this]()
                        {
                    live_socket_active_ = true;
                    manual_socket_close_ = false;
                    socket_label_->setText(QStringLiteral("Live sync: connected"));
                    appendLog(QStringLiteral("Live sync connected")); },
                        Qt::QueuedConnection);

                connect(socket_, &QWebSocket::disconnected, this, [this]()
                        {
                    live_socket_active_ = false;
                    socket_label_->setText(QStringLiteral("Live sync: disconnected"));
                    appendLog(QStringLiteral("Live sync disconnected"));
                    const bool should_reconnect = live_sync_requested_ && !manual_socket_close_;
                    manual_socket_close_ = false;
                    if (should_reconnect)
                    {
                        scheduleReconnect();
                    } },
                        Qt::QueuedConnection);

                connect(socket_, &QWebSocket::textMessageReceived, this, [this](const QString &payload)
                        { handleSocketMessage(payload); },
                        Qt::QueuedConnection);
            },
            Qt::BlockingQueuedConnection);
    }

    void TripClientWindow::destroyNetworkSocket()
    {
        QMetaObject::invokeMethod(
            &network_context_,
            [this]()
            {
                if (socket_ == nullptr)
                {
                    return;
                }

                QObject::disconnect(socket_, nullptr, this, nullptr);
                socket_->close();
                delete socket_;
                socket_ = nullptr;
            },
            Qt::BlockingQueuedConnection);
    }

    void TripClientWindow::applyUiPolish()
    {
        setObjectName(QStringLiteral("TripClientWindow"));
        setStyleSheet(QStringLiteral(R"(
QWidget#TripClientWindow {
    background: #f4efe6;
    color: #28313a;
    font-family: "Segoe UI", "Verdana";
    font-size: 10pt;
}
QGroupBox {
    background: #fffaf1;
    border: 1px solid #dfd2be;
    border-radius: 16px;
    margin-top: 18px;
    padding: 18px 14px 14px 14px;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 16px;
    padding: 0 8px;
    color: #6c4d2f;
}
QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QDateEdit {
    background: #fffdf8;
    border: 1px solid #d7c7af;
    border-radius: 10px;
    min-height: 28px;
    padding: 5px 9px;
    selection-background-color: #d88145;
}
QTextEdit, QPlainTextEdit {
    min-height: 52px;
}
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QDateEdit:focus {
    border: 1px solid #c86b32;
    background: #ffffff;
}
QPushButton {
    background: #2f5d62;
    border: 0;
    border-radius: 11px;
    color: #ffffff;
    font-weight: 600;
    min-height: 30px;
    padding: 6px 13px;
}
QPushButton:hover {
    background: #386f75;
}
QPushButton:pressed {
    background: #23474b;
}
QPushButton:disabled {
    background: #b8b1a8;
    color: #f4efe6;
}
QListWidget, QTableWidget {
    background: #fffdf8;
    alternate-background-color: #f4eadb;
    border: 1px solid #dfd2be;
    border-radius: 12px;
    gridline-color: #eadfce;
    selection-background-color: #d88145;
    selection-color: #ffffff;
}
QHeaderView::section {
    background: #e7d8c1;
    border: 0;
    border-right: 1px solid #d3c2aa;
    color: #3a332b;
    font-weight: 700;
    padding: 8px;
}
QTabWidget::pane {
    border: 1px solid #dfd2be;
    border-radius: 14px;
    top: -1px;
    background: #fffaf1;
}
QTabBar::tab {
    background: #e8dcc9;
    border-top-left-radius: 11px;
    border-top-right-radius: 11px;
    color: #5b4a3a;
    font-weight: 600;
    margin-right: 4px;
    padding: 10px 18px;
}
QTabBar::tab:selected {
    background: #fffaf1;
    color: #2f5d62;
}
QSplitter::handle {
    background: #dfd2be;
}
QScrollArea {
    background: transparent;
    border: 0;
}
QLabel#networkStatus {
    background: #eaf3ea;
    border-radius: 10px;
    color: #31533d;
    font-weight: 600;
    padding: 6px 9px;
}
QLabel#networkStatus[state="busy"] {
    background: #fff0d6;
    color: #7b4b19;
}
)"));

        for (auto *button : findChildren<QPushButton *>())
        {
            button->setCursor(Qt::PointingHandCursor);
            button->setMinimumHeight(36);
        }

        for (auto *line_edit : findChildren<QLineEdit *>())
        {
            line_edit->setMinimumHeight(34);
        }

        for (auto *combo : findChildren<QComboBox *>())
        {
            combo->setMinimumHeight(34);
        }

        for (auto *date_edit : findChildren<QDateEdit *>())
        {
            date_edit->setMinimumHeight(34);
        }

        for (auto *form : findChildren<QFormLayout *>())
        {
            form->setHorizontalSpacing(12);
            form->setVerticalSpacing(10);
        }

        for (auto *table : findChildren<QTableWidget *>())
        {
            table->setAlternatingRowColors(true);
            table->setShowGrid(false);
            table->horizontalHeader()->setHighlightSections(false);
            table->verticalHeader()->setDefaultSectionSize(32);
        }

        if (network_label_ != nullptr)
        {
            network_label_->setProperty("state", QStringLiteral("idle"));
        }
    }

    void TripClientWindow::appendLog(const QString &line)
    {
        log_text_->appendPlainText(line);
    }

    void TripClientWindow::appendResult(const QString &prefix, const trip::QtApiResult &result)
    {
        QString line = prefix + QStringLiteral(": status=") + result.status;
        if (!result.message.isEmpty())
        {
            line += QStringLiteral(", message=") + result.message;
        }
        if (result.http_status != 0)
        {
            line += QStringLiteral(", http=") + QString::number(result.http_status);
        }
        appendLog(line);
    }

    void TripClientWindow::runNetworkOperation(const QString &prefix, ApiTask task, ApiHandler on_success, ApiHandler on_failure)
    {
        const QString base_url = base_url_edit_->text().trimmed();
        const QPointer<TripClientWindow> self(this);
        setNetworkBusy(true, prefix);

        const bool queued = QMetaObject::invokeMethod(
            &network_context_,
            [this, self, prefix, base_url, task = std::move(task), on_success = std::move(on_success), on_failure = std::move(on_failure)]() mutable
            {
                if (self.isNull())
                {
                    return;
                }

                network_client_.setBaseUrl(base_url);
                const trip::QtApiResult result = task(network_client_);

                QMetaObject::invokeMethod(
                    this,
                    [self, prefix, result, on_success = std::move(on_success), on_failure = std::move(on_failure)]() mutable
                    {
                        if (self.isNull())
                        {
                            return;
                        }

                        self->appendResult(prefix, result);
                        self->setNetworkBusy(false, prefix);
                        if (result.ok)
                        {
                            if (on_success)
                            {
                                on_success(result);
                            }
                        }
                        else if (on_failure)
                        {
                            on_failure(result);
                        }
                    },
                    Qt::QueuedConnection);
            },
            Qt::QueuedConnection);

        if (!queued)
        {
            setNetworkBusy(false, prefix);
            appendLog(prefix + QStringLiteral(": failed to queue network task"));
        }
    }

    void TripClientWindow::runNetworkMutation(
        const QString &prefix,
        std::function<trip::QtApiResult(trip::QtTripClient &, quint64)> task,
        ApiHandler on_success)
    {
        if (!hasCurrentTrip())
        {
            appendLog(QStringLiteral("Select a trip first"));
            return;
        }

        const QString token = token_;
        const QString trip_id = current_trip_id_;
        runNetworkOperation(
            prefix,
            [token, trip_id, task = std::move(task)](trip::QtTripClient &client) mutable
            {
                const auto revision_result = client.getRevision(token, trip_id);
                if (!revision_result.ok)
                {
                    return revision_result;
                }

                const quint64 revision = revision_result.payload.value(QStringLiteral("revision")).toVariant().toULongLong();
                return task(client, revision);
            },
            std::move(on_success));
    }

    void TripClientWindow::setNetworkBusy(bool busy, const QString &operation)
    {
        if (busy)
        {
            ++pending_network_jobs_;
        }
        else if (pending_network_jobs_ > 0)
        {
            --pending_network_jobs_;
        }

        if (network_label_ == nullptr)
        {
            return;
        }

        const bool has_pending_jobs = pending_network_jobs_ > 0;
        network_label_->setProperty("state", has_pending_jobs ? QStringLiteral("busy") : QStringLiteral("idle"));
        network_label_->setText(has_pending_jobs
                                    ? QStringLiteral("Network: %1 queued (%2)").arg(operation).arg(pending_network_jobs_)
                                    : QStringLiteral("Network: idle"));
        network_label_->style()->unpolish(network_label_);
        network_label_->style()->polish(network_label_);
        network_label_->update();
    }

    bool TripClientWindow::hasSession() const
    {
        return !token_.isEmpty();
    }

    bool TripClientWindow::hasCurrentTrip() const
    {
        return hasSession() && !current_trip_id_.isEmpty();
    }

    void TripClientWindow::refreshTripsList(bool keep_selection, LoadHandler on_loaded)
    {
        if (!hasSession())
        {
            trips_list_->clear();
            return;
        }

        const QString token = token_;
        const QString previously_selected = keep_selection ? selectedTripId() : current_trip_id_;
        runNetworkOperation(
            QStringLiteral("List Trips"),
            [token](trip::QtTripClient &client)
            {
                return client.listTrips(token);
            },
            [this, token, previously_selected, on_loaded = std::move(on_loaded)](const trip::QtApiResult &result) mutable
            {
                if (token != token_)
                {
                    return;
                }

                populateTripsList(result.payload.value(QStringLiteral("trips")).toArray());
                if (!previously_selected.isEmpty())
                {
                    selectTrip(previously_selected);
                }
                if (on_loaded)
                {
                    on_loaded();
                }
            });
    }

    void TripClientWindow::selectTrip(const QString &trip_id)
    {
        for (int i = 0; i < trips_list_->count(); ++i)
        {
            auto *item = trips_list_->item(i);
            if (item->data(Qt::UserRole).toString() == trip_id)
            {
                trips_list_->setCurrentItem(item);
                return;
            }
        }
    }

    void TripClientWindow::loadCurrentTrip(LoadHandler on_loaded)
    {
        if (!hasCurrentTrip())
        {
            trip_id_label_->setText(QStringLiteral("Trip ID: <none>"));
            revision_label_->setText(QStringLiteral("Revision: 0"));
            counts_label_->setText(QStringLiteral("Days: 0 | Tasks: 0 | Expenses: 0 | Members: 0"));
            populateMembersTable({});
            populateDaysTable({});
            populatePlanTable({});
            populateTasksTable({});
            populateExpensesTable({});
            populateChatTable({});
            populateEventsTable({});
            populateSearchResults({});
            budget_summary_text_->clear();
            raw_json_text_->clear();
            current_trip_ = {};
            clearTaskEditor();
            if (on_loaded)
            {
                on_loaded();
            }
            return;
        }

        const QString token = token_;
        const QString trip_id = current_trip_id_;
        const QString previously_selected_task = selectedTaskId();
        runNetworkOperation(
            QStringLiteral("Snapshot"),
            [token, trip_id](trip::QtTripClient &client)
            {
                return client.getSnapshot(token, trip_id);
            },
            [this, token, trip_id, previously_selected_task, on_loaded = std::move(on_loaded)](const trip::QtApiResult &result) mutable
            {
                if (token != token_ || trip_id != current_trip_id_)
                {
                    return;
                }

                current_trip_ = result.payload.value(QStringLiteral("trip")).toObject();
                current_revision_ = result.payload.value(QStringLiteral("revision")).toVariant().toULongLong();
                last_seen_revision_ = current_revision_;
                trip_id_label_->setText(QStringLiteral("Trip ID: ") + trip_id);
                revision_label_->setText(QStringLiteral("Revision: ") + QString::number(current_revision_));
                counts_label_->setText(
                    QStringLiteral("Days: %1 | Tasks: %2 | Expenses: %3 | Members: %4")
                        .arg(result.payload.value(QStringLiteral("days_count")).toInt())
                        .arg(result.payload.value(QStringLiteral("tasks_count")).toInt())
                        .arg(result.payload.value(QStringLiteral("expenses_count")).toInt())
                        .arg(result.payload.value(QStringLiteral("members_count")).toInt()));

                const QJsonObject info = current_trip_.value(QStringLiteral("info")).toObject();
                title_edit_->setText(info.value(QStringLiteral("title")).toString());
                start_date_edit_->setDate(QDate::fromString(info.value(QStringLiteral("start_date")).toString(), QStringLiteral("yyyy-MM-dd")));
                end_date_edit_->setDate(QDate::fromString(info.value(QStringLiteral("end_date")).toString(), QStringLiteral("yyyy-MM-dd")));
                description_edit_->setPlainText(info.value(QStringLiteral("description")).toString());
                budget_currency_edit_->setText(current_trip_.value(QStringLiteral("budget")).toObject().value(QStringLiteral("currency")).toString());
                budget_limit_edit_->setText(QString::number(current_trip_.value(QStringLiteral("budget")).toObject().value(QStringLiteral("total_limit")).toDouble()));

                populateMembersTable(current_trip_.value(QStringLiteral("members")).toObject());
                populateDaysTable(current_trip_.value(QStringLiteral("days")).toArray());
                if (days_table_->rowCount() > 0 && days_table_->currentRow() < 0)
                {
                    days_table_->selectRow(0);
                }
                populatePlanTable(selectedDayObject());
                populateTasksTable(current_trip_.value(QStringLiteral("tasks")).toArray());
                if (tasks_table_->rowCount() > 0)
                {
                    if (!previously_selected_task.isEmpty())
                    {
                        selectTask(previously_selected_task);
                    }
                    if (tasks_table_->currentRow() < 0)
                    {
                        tasks_table_->selectRow(0);
                    }
                }
                else
                {
                    clearTaskEditor();
                }
                populateExpensesTable(current_trip_.value(QStringLiteral("expenses")).toArray());
                populateChatTable(current_trip_.value(QStringLiteral("chat")).toArray());
                populateEventsTable(current_trip_.value(QStringLiteral("events")).toArray());
                populateRawJson(QString::fromUtf8(QJsonDocument(current_trip_).toJson(QJsonDocument::Indented)));
                refreshMemberEditors();
                refreshBudgetSummary();
                if (on_loaded)
                {
                    on_loaded();
                }
            });
    }

    void TripClientWindow::refreshBudgetSummary()
    {
        if (!hasCurrentTrip())
        {
            budget_summary_text_->clear();
            return;
        }

        budget_summary_text_->setPlainText(QStringLiteral("Loading budget summary..."));
        const QString token = token_;
        const QString trip_id = current_trip_id_;
        runNetworkOperation(
            QStringLiteral("Budget Summary"),
            [token, trip_id](trip::QtTripClient &client)
            {
                return client.getBudgetSummary(token, trip_id);
            },
            [this, token, trip_id](const trip::QtApiResult &result)
            {
                if (token != token_ || trip_id != current_trip_id_)
                {
                    return;
                }

                const QJsonObject summary = result.payload.value(QStringLiteral("summary")).toObject();
                QStringList lines;
                lines << QStringLiteral("Total: %1").arg(summary.value(QStringLiteral("total_expenses")).toDouble());
                const auto by_category = summary.value(QStringLiteral("by_category")).toObject();
                for (auto it = by_category.begin(); it != by_category.end(); ++it)
                {
                    lines << QStringLiteral("Category %1: %2").arg(it.key()).arg(it.value().toDouble());
                }
                const auto balance = summary.value(QStringLiteral("balance_by_user")).toObject();
                for (auto it = balance.begin(); it != balance.end(); ++it)
                {
                    lines << QStringLiteral("Balance %1: %2").arg(it.key()).arg(it.value().toDouble());
                }
                budget_summary_text_->setPlainText(lines.join(QLatin1Char('\n')));
            },
            [this, token, trip_id](const trip::QtApiResult &result)
            {
                if (token == token_ && trip_id == current_trip_id_)
                {
                    budget_summary_text_->setPlainText(QStringLiteral("Budget summary unavailable: ") + result.status);
                }
            });
    }

    void TripClientWindow::refreshMemberEditors()
    {
        task_assignee_combo_->clear();
        expense_payer_combo_->clear();
        expense_day_combo_->clear();

        const QJsonObject members = current_trip_.value(QStringLiteral("members")).toObject();
        task_assignee_combo_->addItem(QStringLiteral("<none>"), QString());
        expense_payer_combo_->addItem(QStringLiteral("<none>"), QString());
        for (auto it = members.begin(); it != members.end(); ++it)
        {
            task_assignee_combo_->addItem(it.key() + QStringLiteral(" (") + it.value().toString() + QStringLiteral(")"), it.key());
            expense_payer_combo_->addItem(it.key() + QStringLiteral(" (") + it.value().toString() + QStringLiteral(")"), it.key());
        }

        expense_day_combo_->addItem(QStringLiteral("<none>"), QString());
        for (const auto &value : current_trip_.value(QStringLiteral("days")).toArray())
        {
            const QJsonObject day = value.toObject();
            expense_day_combo_->addItem(day.value(QStringLiteral("name")).toString(), day.value(QStringLiteral("id")).toString());
        }
    }

    void TripClientWindow::populateTripsList(const QJsonArray &trips)
    {
        trips_list_->clear();
        for (const auto &value : trips)
        {
            const QJsonObject trip = value.toObject();
            const QString title = trip.value(QStringLiteral("info")).toObject().value(QStringLiteral("title")).toString();
            const QString role = trip.value(QStringLiteral("my_role")).toString();
            auto *item = new QListWidgetItem(title + QStringLiteral(" [") + role + QStringLiteral("]"));
            item->setData(Qt::UserRole, trip.value(QStringLiteral("id")).toString());
            trips_list_->addItem(item);
        }
    }

    void TripClientWindow::populateMembersTable(const QJsonObject &members)
    {
        members_table_->setRowCount(0);
        int row = 0;
        for (auto it = members.begin(); it != members.end(); ++it)
        {
            members_table_->insertRow(row);
            members_table_->setItem(row, 0, new QTableWidgetItem(it.key()));
            members_table_->setItem(row, 1, new QTableWidgetItem(it.value().toString()));
            ++row;
        }
    }

    void TripClientWindow::populateDaysTable(const QJsonArray &days)
    {
        days_table_->setRowCount(0);
        for (int row = 0; row < days.size(); ++row)
        {
            const QJsonObject day = days[row].toObject();
            auto *id_item = new QTableWidgetItem(day.value(QStringLiteral("id")).toString());
            id_item->setData(Qt::UserRole, day.value(QStringLiteral("id")).toString());
            days_table_->insertRow(row);
            days_table_->setItem(row, 0, id_item);
            days_table_->setItem(row, 1, new QTableWidgetItem(day.value(QStringLiteral("name")).toString()));
        }
    }

    void TripClientWindow::populatePlanTable(const QJsonObject &selected_day)
    {
        plan_table_->setRowCount(0);
        if (selected_day.isEmpty())
        {
            selected_day_label_->setText(QStringLiteral("Selected day: <none>"));
            return;
        }

        selected_day_label_->setText(QStringLiteral("Selected day: ") + selected_day.value(QStringLiteral("name")).toString());
        const QJsonArray items = selected_day.value(QStringLiteral("items")).toArray();
        for (int row = 0; row < items.size(); ++row)
        {
            const QJsonObject item = items[row].toObject();
            auto *name_item = new QTableWidgetItem(item.value(QStringLiteral("name")).toString());
            name_item->setData(Qt::UserRole, item.value(QStringLiteral("id")).toString());
            plan_table_->insertRow(row);
            plan_table_->setItem(row, 0, name_item);
            plan_table_->setItem(row, 1, new QTableWidgetItem(item.value(QStringLiteral("time")).toString()));
            plan_table_->setItem(row, 2, new QTableWidgetItem(item.value(QStringLiteral("category")).toString()));
            plan_table_->setItem(row, 3, new QTableWidgetItem(item.value(QStringLiteral("link")).toString()));
            plan_table_->setItem(row, 4, new QTableWidgetItem(item.value(QStringLiteral("notes")).toString()));
        }
    }

    void TripClientWindow::populateTasksTable(const QJsonArray &tasks)
    {
        tasks_table_->setRowCount(0);
        for (int row = 0; row < tasks.size(); ++row)
        {
            const QJsonObject task = tasks[row].toObject();
            auto *text_item = new QTableWidgetItem(task.value(QStringLiteral("text")).toString());
            text_item->setData(Qt::UserRole, task.value(QStringLiteral("id")).toString());
            tasks_table_->insertRow(row);
            tasks_table_->setItem(row, 0, text_item);
            tasks_table_->setItem(row, 1, new QTableWidgetItem(task.value(QStringLiteral("done")).toBool() ? QStringLiteral("true") : QStringLiteral("false")));
            tasks_table_->setItem(row, 2, new QTableWidgetItem(task.value(QStringLiteral("assignee_user_id")).toString()));
            tasks_table_->setItem(row, 3, new QTableWidgetItem(task.value(QStringLiteral("deadline")).toString()));
        }
    }

    void TripClientWindow::selectTask(const QString &task_id)
    {
        if (task_id.isEmpty())
        {
            tasks_table_->clearSelection();
            tasks_table_->setCurrentItem(nullptr);
            return;
        }

        for (int row = 0; row < tasks_table_->rowCount(); ++row)
        {
            auto *item = tasks_table_->item(row, 0);
            if (item != nullptr && item->data(Qt::UserRole).toString() == task_id)
            {
                tasks_table_->setCurrentCell(row, 0);
                tasks_table_->selectRow(row);
                return;
            }
        }

        tasks_table_->clearSelection();
        tasks_table_->setCurrentItem(nullptr);
    }

    void TripClientWindow::clearTaskEditor()
    {
        task_text_edit_->clear();
        task_deadline_edit_->clear();
        task_done_check_->setChecked(false);
        if (task_assignee_combo_->count() > 0)
        {
            task_assignee_combo_->setCurrentIndex(0);
        }
        else
        {
            task_assignee_combo_->setCurrentIndex(-1);
        }
    }

    void TripClientWindow::populateExpensesTable(const QJsonArray &expenses)
    {
        expenses_table_->setRowCount(0);
        for (int row = 0; row < expenses.size(); ++row)
        {
            const QJsonObject expense = expenses[row].toObject();
            expenses_table_->insertRow(row);
            expenses_table_->setItem(row, 0, new QTableWidgetItem(QString::number(expense.value(QStringLiteral("amount")).toDouble())));
            expenses_table_->setItem(row, 1, new QTableWidgetItem(expense.value(QStringLiteral("category")).toString()));
            expenses_table_->setItem(row, 2, new QTableWidgetItem(expense.value(QStringLiteral("paid_by_user_id")).toString()));
            expenses_table_->setItem(row, 3, new QTableWidgetItem(expense.value(QStringLiteral("date")).toString()));
            expenses_table_->setItem(row, 4, new QTableWidgetItem(expense.value(QStringLiteral("comment")).toString()));
        }
    }

    void TripClientWindow::populateChatTable(const QJsonArray &messages)
    {
        chat_table_->setRowCount(0);
        for (int row = 0; row < messages.size(); ++row)
        {
            const QJsonObject message = messages[row].toObject();
            chat_table_->insertRow(row);
            chat_table_->setItem(row, 0, new QTableWidgetItem(message.value(QStringLiteral("user_id")).toString()));
            chat_table_->setItem(row, 1, new QTableWidgetItem(message.value(QStringLiteral("text")).toString()));
            chat_table_->setItem(row, 2, new QTableWidgetItem(QString::number(message.value(QStringLiteral("timestamp_ms")).toVariant().toLongLong())));
        }
    }

    void TripClientWindow::populateEventsTable(const QJsonArray &events)
    {
        events_table_->setRowCount(0);
        for (int row = 0; row < events.size(); ++row)
        {
            const QJsonObject event = events[row].toObject();
            events_table_->insertRow(row);
            events_table_->setItem(row, 0, new QTableWidgetItem(QString::number(event.value(QStringLiteral("revision")).toVariant().toULongLong())));
            events_table_->setItem(row, 1, new QTableWidgetItem(event.value(QStringLiteral("actor_user_id")).toString()));
            events_table_->setItem(row, 2, new QTableWidgetItem(event.value(QStringLiteral("action")).toString()));
            events_table_->setItem(row, 3, new QTableWidgetItem(event.value(QStringLiteral("entity")).toString()));
            events_table_->setItem(row, 4, new QTableWidgetItem(event.value(QStringLiteral("details")).toString()));
        }
    }

    void TripClientWindow::populateSearchResults(const QJsonArray &hits)
    {
        search_results_table_->setRowCount(0);
        for (int row = 0; row < hits.size(); ++row)
        {
            const QJsonObject hit = hits[row].toObject();
            search_results_table_->insertRow(row);
            search_results_table_->setItem(row, 0, new QTableWidgetItem(hit.value(QStringLiteral("kind")).toString()));
            search_results_table_->setItem(row, 1, new QTableWidgetItem(hit.value(QStringLiteral("id")).toString()));
            search_results_table_->setItem(row, 2, new QTableWidgetItem(hit.value(QStringLiteral("text")).toString()));
        }
    }

    void TripClientWindow::populateRawJson(const QString &json_text)
    {
        const QJsonDocument parsed = QJsonDocument::fromJson(json_text.toUtf8());
        if (!parsed.isNull())
        {
            raw_json_text_->setPlainText(QString::fromUtf8(parsed.toJson(QJsonDocument::Indented)));
        }
        else
        {
            raw_json_text_->setPlainText(json_text);
        }
    }

    QString TripClientWindow::selectedTripId() const
    {
        auto *item = trips_list_->currentItem();
        return item == nullptr ? QString() : item->data(Qt::UserRole).toString();
    }

    QString TripClientWindow::selectedMemberId() const
    {
        const int row = members_table_->currentRow();
        if (row < 0 || members_table_->item(row, 0) == nullptr)
        {
            return {};
        }
        return members_table_->item(row, 0)->text();
    }

    QString TripClientWindow::selectedDayId() const
    {
        const int row = days_table_->currentRow();
        if (row < 0 || days_table_->item(row, 0) == nullptr)
        {
            return {};
        }
        return days_table_->item(row, 0)->data(Qt::UserRole).toString();
    }

    QString TripClientWindow::selectedPlanItemId() const
    {
        const int row = plan_table_->currentRow();
        if (row < 0 || plan_table_->item(row, 0) == nullptr)
        {
            return {};
        }
        return plan_table_->item(row, 0)->data(Qt::UserRole).toString();
    }

    QString TripClientWindow::selectedTaskId() const
    {
        const int row = tasks_table_->currentRow();
        if (row < 0 || tasks_table_->item(row, 0) == nullptr)
        {
            return {};
        }
        return tasks_table_->item(row, 0)->data(Qt::UserRole).toString();
    }

    QJsonObject TripClientWindow::selectedDayObject() const
    {
        const QString day_id = selectedDayId();
        if (day_id.isEmpty())
        {
            return {};
        }
        for (const auto &value : current_trip_.value(QStringLiteral("days")).toArray())
        {
            const QJsonObject day = value.toObject();
            if (day.value(QStringLiteral("id")).toString() == day_id)
            {
                return day;
            }
        }
        return {};
    }

    QStringList TripClientWindow::currentDayOrder() const
    {
        QStringList ids;
        for (const auto &value : current_trip_.value(QStringLiteral("days")).toArray())
        {
            ids.push_back(value.toObject().value(QStringLiteral("id")).toString());
        }
        return ids;
    }

    QStringList TripClientWindow::currentPlanOrder(const QJsonObject &day) const
    {
        QStringList ids;
        for (const auto &value : day.value(QStringLiteral("items")).toArray())
        {
            ids.push_back(value.toObject().value(QStringLiteral("id")).toString());
        }
        return ids;
    }

    void TripClientWindow::connectLiveUpdates()
    {
        if (!hasCurrentTrip())
        {
            appendLog(QStringLiteral("Select a trip before live sync"));
            return;
        }

        live_sync_requested_ = true;
        reconnect_timer_.stop();
        const QNetworkRequest request = client_.updatesWebSocketRequest(token_, current_trip_id_, last_seen_revision_);
        const bool close_before_open = live_socket_active_;
        manual_socket_close_ = close_before_open;
        live_socket_active_ = true;
        socket_label_->setText(QStringLiteral("Live sync: connecting..."));

        const QPointer<TripClientWindow> self(this);
        const bool queued = QMetaObject::invokeMethod(
            &network_context_,
            [this, self, request, close_before_open]()
            {
                if (self.isNull() || socket_ == nullptr)
                {
                    return;
                }

                const auto open_socket = [this, self, request]()
                {
                    if (self.isNull() || socket_ == nullptr)
                    {
                        return;
                    }

                    QMetaObject::invokeMethod(
                        this,
                        [self]()
                        {
                            if (!self.isNull())
                            {
                                self->manual_socket_close_ = false;
                                self->socket_label_->setText(QStringLiteral("Live sync: connecting..."));
                            }
                        },
                        Qt::QueuedConnection);
                    socket_->open(request);
                };

                if (close_before_open && socket_->state() != QAbstractSocket::UnconnectedState)
                {
                    socket_->close();
                    QTimer::singleShot(150, socket_, open_socket);
                    return;
                }

                open_socket();
            },
            Qt::QueuedConnection);

        if (!queued)
        {
            live_socket_active_ = false;
            manual_socket_close_ = false;
            socket_label_->setText(QStringLiteral("Live sync: disconnected"));
            appendLog(QStringLiteral("Failed to queue live sync connection"));
        }
    }

    void TripClientWindow::disconnectLiveUpdates(bool manual_disconnect)
    {
        if (manual_disconnect)
        {
            live_sync_requested_ = false;
        }
        reconnect_timer_.stop();
        manual_socket_close_ = true;
        live_socket_active_ = false;
        socket_label_->setText(QStringLiteral("Live sync: disconnected"));

        const QPointer<TripClientWindow> self(this);
        const bool queued = QMetaObject::invokeMethod(
            &network_context_,
            [this, self]()
            {
                if (self.isNull() || socket_ == nullptr)
                {
                    return;
                }

                socket_->close();
            },
            Qt::QueuedConnection);

        if (!queued)
        {
            manual_socket_close_ = false;
            appendLog(QStringLiteral("Failed to queue live sync disconnect"));
        }
    }

    void TripClientWindow::scheduleReconnect()
    {
        socket_label_->setText(QStringLiteral("Live sync: reconnecting..."));
        if (!reconnect_timer_.isActive())
        {
            reconnect_timer_.start();
        }
    }

    void TripClientWindow::handleSocketMessage(const QString &payload)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8());
        if (doc.isObject())
        {
            const QJsonObject root = doc.object();
            const QJsonObject event = root.value(QStringLiteral("event")).toObject();
            quint64 revision = 0;
            bool has_revision = false;
            const QJsonValue revision_value = event.value(QStringLiteral("revision"));
            if (revision_value.isDouble())
            {
                revision = revision_value.toVariant().toULongLong(&has_revision);
            }
            else if (revision_value.isString())
            {
                revision = revision_value.toString().toULongLong(&has_revision);
            }
            if (has_revision && revision > last_seen_revision_)
            {
                last_seen_revision_ = revision;
            }
            appendLog(
                QStringLiteral("Live event: rev=%1 %2/%3")
                    .arg(revision)
                    .arg(event.value(QStringLiteral("action")).toString())
                    .arg(event.value(QStringLiteral("entity")).toString()));
        }
        else
        {
            appendLog(QStringLiteral("Live payload: ") + payload);
        }

        if (hasCurrentTrip())
        {
            reload_debounce_timer_.start();
        }
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TripRoom"));
    QCoreApplication::setApplicationName(QStringLiteral("TripRoomQtClient"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Trip Planner Qt Client"));
    parser.addHelpOption();
    QCommandLineOption server_url_option(
        QStringList{QStringLiteral("s"), QStringLiteral("server-url")},
        QStringLiteral("Base server URL"),
        QStringLiteral("url"));
    parser.addOption(server_url_option);
    parser.process(app);

    QSettings settings;
    QString server_url = parser.value(server_url_option);
    if (server_url.isEmpty())
    {
        server_url = settings.value(QStringLiteral("server_url"), defaultServerUrl()).toString();
    }
    else
    {
        settings.setValue(QStringLiteral("server_url"), server_url);
    }

    TripClientWindow window(server_url);
    window.show();
    return app.exec();
}

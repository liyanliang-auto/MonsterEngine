#include "WorkbenchWindow.h"
#include "NativePreviewWidget.h"
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QDebug>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleHints>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>

namespace MonsterStudioQt {
namespace {
QString Missing(const QString& value) { return value.isEmpty() ? QStringLiteral("未记录") : value; }
QLabel* Label(const QString& text,QWidget* parent=nullptr) {
    auto* label=new QLabel(text,parent); label->setWordWrap(true); label->setTextFormat(Qt::PlainText); return label;
}
QPushButton* Button(const QString& text,QLayout* layout) {
    auto* button=new QPushButton(text); layout->addWidget(button); return button;
}
QWidget* Card(QVBoxLayout* parent,QVBoxLayout*& layout) {
    auto* card=new QWidget; card->setObjectName(QStringLiteral("card")); layout=new QVBoxLayout(card); layout->setContentsMargins(20,20,20,20); layout->setSpacing(12); parent->addWidget(card); return card;
}
QLabel* Title(const QString& text,const char* role) {
    auto* label=Label(text);label->setObjectName(QString::fromLatin1(role));return label;
}
QScrollArea* Scroll(QWidget* content) {
    auto* scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidget(content); return scroll;
}
}

WorkbenchWindow::WorkbenchWindow(QWidget* parent):QMainWindow(parent) {
    setWindowTitle(QStringLiteral("MonsterStudio - 视频重建"));
    QFont bodyFont=font(); bodyFont.setPixelSize(14); setFont(bodyFont);
    setAcceptDrops(true);
    auto* shell=new QWidget(this); auto* shellLayout=new QVBoxLayout(shell); shellLayout->setContentsMargins(0,0,0,0); shellLayout->setSpacing(0);
    auto* body=new QHBoxLayout; body->setContentsMargins(12,12,12,12); body->setSpacing(20); shellLayout->addLayout(body,1);
    nav_=new QWidget; nav_->setObjectName(QStringLiteral("navigation")); nav_->setFixedWidth(136); auto* navigation=new QVBoxLayout(nav_); navigation->setContentsMargins(12,20,12,12); navigation->setSpacing(8);
    auto* mark=Title({},"brandMark");mark->setAccessibleName(QStringLiteral("M"));mark->setAlignment(Qt::AlignCenter);mark->setFixedSize(36,36);
    QPixmap brand(QStringLiteral(":/monster/monster.png"));
    if (brand.isNull()) qWarning("Embedded Monster brand image is missing");
    else { brand=brand.scaled(QSize(36,36)*devicePixelRatioF(),Qt::KeepAspectRatio,Qt::SmoothTransformation);brand.setDevicePixelRatio(devicePixelRatioF());mark->setPixmap(brand); }
    navigation->addWidget(mark);
    navigation->addWidget(Title(QStringLiteral("MONSTER\nSTUDIO"),"brand"));navigation->addWidget(Title(QStringLiteral("本地三维重建"),"support"));navigation->addSpacing(28);
    auto* tasks=Button(QStringLiteral("任务"),navigation); auto* results=Button(QStringLiteral("结果"),navigation); navigation->addStretch(); auto* settings=Button(QStringLiteral("设置"),navigation);
    for(auto* button:{tasks,results,settings}){button->setObjectName(QStringLiteral("navButton"));button->setCheckable(true);}
    body->addWidget(nav_); pages_=new QStackedWidget; body->addWidget(pages_,1);
    auto* work=new QWidget; auto* workLayout=new QVBoxLayout(work); workLayout->setContentsMargins(0,12,8,4); workLayout->setSpacing(24);
    auto* heading=new QHBoxLayout;auto* introduction=new QVBoxLayout;introduction->setSpacing(6); introduction->addWidget(Title(QStringLiteral("从视频，走进三维。"),"pageTitle"));introduction->addWidget(Title(QStringLiteral("创建你的三维作品，或继续最近的项目。"),"support"));heading->addLayout(introduction,1); open_=Button(QStringLiteral("打开项目…"),heading); workLayout->addLayout(heading);
    workColumns_=new QSplitter(Qt::Horizontal);workColumns_->setHandleWidth(20); workColumns_->setChildrenCollapsible(false); workLayout->addWidget(workColumns_,1);
    newForm_=new QWidget;newForm_->setObjectName(QStringLiteral("card")); auto* formLayout=new QVBoxLayout(newForm_); formLayout->setContentsMargins(20,20,20,20); formLayout->setSpacing(12);
    formLayout->addWidget(Title(QStringLiteral("新建重建"),"sectionTitle"));
    auto* importArea=new QWidget;importArea->setObjectName(QStringLiteral("importArea"));auto* importLayout=new QVBoxLayout(importArea);importLayout->setContentsMargins(16,18,16,18);importLayout->setSpacing(10);
    auto* videoMark=Title(QStringLiteral("VIDEO  →  3D"),"importMark");videoMark->setAlignment(Qt::AlignCenter);importLayout->addWidget(videoMark);
    importHint_=Label(QStringLiteral("拖入一段视频，开始创作"));importHint_->setAlignment(Qt::AlignCenter);importLayout->addWidget(importHint_);
    video_=new QLineEdit; video_->setReadOnly(true); video_->setPlaceholderText(QStringLiteral("支持 MP4、MOV 等本地视频")); video_->setAccessibleName(QStringLiteral("视频文件")); importLayout->addWidget(video_);
    choose_=Button(QStringLiteral("选择视频…"),importLayout);choose_->setObjectName(QStringLiteral("importButton"));formLayout->addWidget(importArea);
    auto* fields=new QFormLayout;fields->setRowWrapPolicy(QFormLayout::WrapAllRows);fields->setVerticalSpacing(8); name_=new QLineEdit;name_->setPlaceholderText(QStringLiteral("选择视频后自动建议名称")); name_->setAccessibleName(QStringLiteral("项目名称")); root_=new QLineEdit; root_->setAccessibleName(QStringLiteral("项目根目录")); fields->addRow(QStringLiteral("项目名称"),name_); fields->addRow(QStringLiteral("保存位置"),root_); formLayout->addLayout(fields);
    auto* rootActions=new QHBoxLayout;rootActions->addStretch();rootChoose_=Button(QStringLiteral("更改保存位置…"),rootActions);rootChoose_->setObjectName(QStringLiteral("quiet"));formLayout->addLayout(rootActions);
    quality_=new QComboBox; quality_->addItems({QStringLiteral("低 · 快速尝试"),QStringLiteral("中 · 默认推荐"),QStringLiteral("高 · 更多训练")}); quality_->setCurrentIndex(1); quality_->setAccessibleName(QStringLiteral("重建画质")); formLayout->addWidget(Label(QStringLiteral("重建画质"))); formLayout->addWidget(quality_);
    auto* advancedGroup=new QGroupBox(QStringLiteral("高级配置（可选）")); advancedGroup_=advancedGroup; advancedGroup->setAccessibleName(QStringLiteral("展开或收起高级配置")); advancedGroup->setCheckable(true); advancedGroup->setChecked(false);
    advancedGroup->setObjectName(QStringLiteral("disclosure"));auto* advancedOuterLayout=new QVBoxLayout(advancedGroup); advancedOuterLayout->setContentsMargins(8,8,8,8);
    auto* advancedContent=new QWidget(advancedGroup); auto* advancedLayout=new QVBoxLayout(advancedContent); advancedLayout->setContentsMargins(0,0,0,0);
    advancedLayout->addWidget(Label(QStringLiteral("普通重建使用已验证的本机配置。仅明确需要时选择自定义配置。"))); advanced_=new QLineEdit; advanced_->setReadOnly(true); advanced_->setAccessibleName(QStringLiteral("高级配置文件")); advancedLayout->addWidget(advanced_); advancedChoose_=Button(QStringLiteral("选择高级配置…"),advancedLayout); advancedOuterLayout->addWidget(advancedContent); advancedContent->hide(); formLayout->addWidget(advancedGroup);
    target_=Title(QStringLiteral("选择视频后显示最终项目目录"),"support"); target_->setTextInteractionFlags(Qt::TextSelectableByMouse); formLayout->addWidget(target_);
    environment_=Title(QStringLiteral("正在检查运行环境"),"support"); formLayout->addWidget(environment_);
    formLayout->addStretch();
    auto* creationColumn=new QWidget;auto* creationLayout=new QVBoxLayout(creationColumn);creationLayout->setContentsMargins(0,0,0,0);creationLayout->setSpacing(12);creationLayout->addWidget(Scroll(newForm_),1);
    start_=Button(QStringLiteral("开始重建"),creationLayout); start_->setObjectName(QStringLiteral("primary"));
    workColumns_->addWidget(creationColumn);
    auto* right=new QWidget; auto* rightLayout=new QVBoxLayout(right); rightLayout->setContentsMargins(0,0,0,0); rightLayout->setSpacing(20);
    QVBoxLayout* currentLayout; Card(rightLayout,currentLayout);currentLayout->addWidget(Title(QStringLiteral("当前任务"),"sectionTitle"));
    currentName_=Title(QStringLiteral("准备好创作了吗？"),"taskTitle"); currentLayout->addWidget(currentName_); stateLabel_=Title(QStringLiteral("选择视频开始，或打开已有项目"),"support"); currentLayout->addWidget(stateLabel_); stage_=Title({},"stage");stage_->setAccessibleName(QStringLiteral("当前处理阶段")); currentLayout->addWidget(stage_);
    stageProgress_=new QProgressBar;stageProgress_->setRange(0,0);stageProgress_->setTextVisible(false);stageProgress_->setAccessibleName(QStringLiteral("当前阶段处理中，尚无可用百分比"));stageProgress_->hide();currentLayout->addWidget(stageProgress_);
    trainingText_=Label({}); currentLayout->addWidget(trainingText_); training_=new QProgressBar; training_->setRange(0,100); training_->setAccessibleName(QStringLiteral("训练进度（仅训练阶段）")); training_->hide(); currentLayout->addWidget(training_);
    activeTime_=Title(QStringLiteral("活动耗时：未记录"),"support"); completedAt_=Title(QStringLiteral("完成时间：未记录"),"support"); currentLayout->addWidget(activeTime_); currentLayout->addWidget(completedAt_);
    auto* controls=new QHBoxLayout; pause_=Button(QStringLiteral("暂停并保存"),controls); resume_=Button(QStringLiteral("恢复训练"),controls); force_=Button(QStringLiteral("强制停止…"),controls); currentLayout->addLayout(controls);
    force_->setObjectName(QStringLiteral("quiet"));auto* technical=new QGroupBox(QStringLiteral("运行详情"));technical->setObjectName(QStringLiteral("disclosure")); technical->setCheckable(true); technical->setChecked(false); auto* technicalLayout=new QVBoxLayout(technical); details_=new QPlainTextEdit; details_->setReadOnly(true); details_->setMaximumHeight(140); details_->hide(); technicalLayout->addWidget(details_); connect(technical,&QGroupBox::toggled,details_,&QWidget::setVisible); currentLayout->addWidget(technical);
    QVBoxLayout* historyLayout;auto* history=Card(rightLayout,historyLayout);rightLayout->setStretchFactor(history,1);historyLayout->addWidget(Title(QStringLiteral("最近项目与执行历史"),"sectionTitle"));historyLayout->addWidget(Title(QStringLiteral("按项目保留每次执行，双击打开。"),"support"));
    historyEmpty_=Title(QStringLiteral("这里将保留你的创作\n创建或打开项目后，即可在此继续。"),"emptyState");historyEmpty_->setAlignment(Qt::AlignCenter);historyEmpty_->setMinimumHeight(120);historyLayout->addWidget(historyEmpty_);
    projects_=new QTreeWidget; projects_->setHeaderLabels({QStringLiteral("项目 / 执行"),QStringLiteral("观察状态"),QStringLiteral("完成时间"),QStringLiteral("活动耗时")}); projects_->setRootIsDecorated(true); projects_->setAlternatingRowColors(false);projects_->setUniformRowHeights(true); projects_->setAccessibleName(QStringLiteral("按项目分组的最近项目与执行历史")); projects_->header()->setSectionResizeMode(0,QHeaderView::Stretch);projects_->header()->setMinimumSectionSize(64); projects_->setMinimumHeight(150); historyLayout->addWidget(projects_,1);
    auto* recentControls=new QHBoxLayout; refresh_=Button(QStringLiteral("刷新当前项目"),recentControls);relocate_=Button(QStringLiteral("重新定位…"),recentControls);remove_=Button(QStringLiteral("移出最近"),recentControls);for(auto* button:{refresh_,relocate_,remove_})button->setObjectName(QStringLiteral("quiet"));historyLayout->addLayout(recentControls);
    workColumns_->addWidget(Scroll(right)); workColumns_->setStretchFactor(0,2); workColumns_->setStretchFactor(1,3); workColumns_->setSizes({420,630}); pages_->addWidget(work);

    resultPage_=new QWidget; auto* resultLayout=new QVBoxLayout(resultPage_); resultLayout->setContentsMargins(0,12,8,4);resultLayout->setSpacing(12);
    auto* toolbar=new QHBoxLayout; auto* back=Button(QStringLiteral("返回任务"),toolbar); resultName_=Label(QStringLiteral("三维结果")); toolbar->addWidget(resultName_,1); reset_=Button(QStringLiteral("复位 (R)"),toolbar); auto* resultInfo=Button(QStringLiteral("详情"),toolbar); resultInfo->setCheckable(true); fullscreenButton_=Button(QStringLiteral("全屏 (F11)"),toolbar); resultLayout->addLayout(toolbar);
    // This native widget is constructed once and never moved between pages/parents.
    preview_=new NativePreviewWidget(resultPage_); preview_->setMinimumSize(320,280); resultLayout->addWidget(preview_,1);
    retry_=Button(QStringLiteral("重新加载结果"),resultLayout); resultDetails_=new QPlainTextEdit; resultDetails_->setReadOnly(true); resultDetails_->setMaximumHeight(140); resultDetails_->hide(); resultLayout->addWidget(resultDetails_); resultTimes_=Label(QStringLiteral("完成时间：未记录    活动耗时：未记录")); resultLayout->addWidget(resultTimes_); resultLayout->addWidget(Label(QStringLiteral("左键旋转 · 滚轮缩放 · R 复位 · F11 全屏 · Esc 退出全屏"))); pages_->addWidget(resultPage_);
    auto* settingsPage=new QWidget; auto* settingsLayout=new QVBoxLayout(settingsPage); settingsLayout->setContentsMargins(24,24,24,24); settingsLayout->setSpacing(16); settingsLayout->addWidget(Label(QStringLiteral("设置"))); theme_=new QComboBox; theme_->addItems({QStringLiteral("跟随系统"),QStringLiteral("浅色"),QStringLiteral("深色")}); theme_->setAccessibleName(QStringLiteral("界面主题")); settingsLayout->addWidget(Label(QStringLiteral("界面主题"))); settingsLayout->addWidget(theme_); settingsRoot_=new QLineEdit; settingsRoot_->setReadOnly(true); settingsLayout->addWidget(Label(QStringLiteral("默认项目根目录"))); settingsLayout->addWidget(settingsRoot_); settingsRootChoose_=Button(QStringLiteral("选择默认根目录…"),settingsLayout); settingsLayout->addWidget(Label(QStringLiteral("更改默认位置不会迁移已有项目。移出最近列表不会删除磁盘项目。"))); settingsEnvironment_=Label(QStringLiteral("正在检查运行环境")); settingsLayout->addWidget(settingsEnvironment_); runtime_=Button(QStringLiteral("检查或设置运行环境…"),settingsLayout); settingsLayout->addStretch(); pages_->addWidget(Scroll(settingsPage));
    resultName_->setObjectName(QStringLiteral("sectionTitle"));resultTimes_->setObjectName(QStringLiteral("support"));settingsPage->setObjectName(QStringLiteral("card"));settingsLayout->itemAt(0)->widget()->setObjectName(QStringLiteral("pageTitle"));settingsRoot_->setAccessibleName(QStringLiteral("默认项目根目录"));settingsEnvironment_->setTextInteractionFlags(Qt::TextSelectableByMouse);resultDetails_->setAccessibleName(QStringLiteral("所选执行结果详情"));details_->setAccessibleName(QStringLiteral("实时运行详情"));
    setCentralWidget(shell); statusBar()->showMessage(QStringLiteral("本地处理 · 视频与项目保存在你的电脑"));statusBar()->setSizeGripEnabled(true);
    connect(pages_,&QStackedWidget::currentChanged,this,[tasks,results,settings](int page){tasks->setChecked(page==0);results->setChecked(page==1);settings->setChecked(page==2);});tasks->setChecked(true);

    connect(tasks,&QPushButton::clicked,this,&WorkbenchWindow::ShowWorkbench); connect(results,&QPushButton::clicked,this,[this]{pages_->setCurrentIndex(1);}); connect(settings,&QPushButton::clicked,this,&WorkbenchWindow::ShowSettings);
    connect(choose_,&QPushButton::clicked,this,&WorkbenchWindow::ChooseVideo);
    connect(open_,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getExistingDirectory(this,QStringLiteral("打开 MonsterStudio 项目"),root_->text()); if(!path.isEmpty()) emit openProjectRequested(path);});
    connect(rootChoose_,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getExistingDirectory(this,QStringLiteral("选择项目根目录"),root_->text()); if(!path.isEmpty()){SetProjectRoot(path); emit rootRequested(path);}});
    connect(settingsRootChoose_,&QPushButton::clicked,rootChoose_,&QPushButton::click);
    connect(root_,&QLineEdit::editingFinished,this,[this]{if(!state_.busy&&!state_.closing&&!root_->text().trimmed().isEmpty()) emit rootRequested(root_->text().trimmed());});
    connect(name_,&QLineEdit::textChanged,this,&WorkbenchWindow::UpdateTarget); connect(root_,&QLineEdit::textChanged,this,&WorkbenchWindow::UpdateTarget);
    connect(quality_,&QComboBox::currentIndexChanged,this,[this]{advanced_->clear();UpdateControls();});
    connect(advancedChoose_,&QPushButton::clicked,this,[this]{const auto file=QFileDialog::getOpenFileName(this,QStringLiteral("选择高级重建配置"),{},QStringLiteral("JSON 配置 (*.json)"));if(!file.isEmpty()){advanced_->setText(file);UpdateControls();}});
    connect(advancedGroup,&QGroupBox::toggled,advancedContent,&QWidget::setVisible);
    connect(advancedGroup,&QGroupBox::toggled,this,[this](bool checked){if(!checked){advanced_->clear();UpdateControls();}});
    connect(start_,&QPushButton::clicked,this,[this]{if(start_->isEnabled())emit startRequested();}); connect(pause_,&QPushButton::clicked,this,&WorkbenchWindow::pauseRequested); connect(resume_,&QPushButton::clicked,this,&WorkbenchWindow::resumeRequested); connect(force_,&QPushButton::clicked,this,&WorkbenchWindow::forceStopRequested);
    connect(refresh_,&QPushButton::clicked,this,&WorkbenchWindow::refreshRequested);
    connect(projects_,&QTreeWidget::itemActivated,this,[this](QTreeWidgetItem* item,int){if(state_.busy||state_.controlPending||state_.closing)return;const auto execution=item->data(0,Qt::UserRole+1).toString();if(!execution.isEmpty()){if(item->data(0,Qt::UserRole+2).toBool())emit executionRequested(execution);}else emit openProjectRequested(item->data(0,Qt::UserRole).toString());});
    connect(remove_,&QPushButton::clicked,this,[this]{auto* item=projects_->currentItem();if(!item)return;while(item->parent())item=item->parent();emit removeRecentRequested(item->data(0,Qt::UserRole).toString());});
    connect(relocate_,&QPushButton::clicked,this,[this]{auto* item=projects_->currentItem();if(!item)return;while(item->parent())item=item->parent();const auto path=QFileDialog::getExistingDirectory(this,QStringLiteral("重新定位已有项目"));if(!path.isEmpty())emit relocateRecentRequested(item->data(0,Qt::UserRole).toString(),path);});
    connect(back,&QPushButton::clicked,this,[this]{ShowWorkbench();emit backRequested();}); connect(reset_,&QPushButton::clicked,preview_,&NativePreviewWidget::ResetView); connect(resultInfo,&QPushButton::toggled,resultDetails_,&QWidget::setVisible); connect(fullscreenButton_,&QPushButton::clicked,this,&WorkbenchWindow::ToggleFullscreen); connect(retry_,&QPushButton::clicked,this,&WorkbenchWindow::retryPreviewRequested);
    connect(preview_,&NativePreviewWidget::statusChanged,this,&WorkbenchWindow::UpdateControls);
    connect(theme_,&QComboBox::currentIndexChanged,this,[this](int value){ApplyTheme(value);emit themeRequested(value);});
    connect(runtime_,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("选择运行环境配置"),{},QStringLiteral("JSON 配置 (*.json)"));if(!path.isEmpty())emit provisionRuntimeRequested(path);});
    connect(qApp->styleHints(),&QStyleHints::colorSchemeChanged,this,[this]{if(themePreference_==0)RefreshPalette();});
    qApp->installEventFilter(this); RefreshPalette(); UpdateControls();
    FitToAvailableGeometry(screen()?screen()->availableGeometry():QRect(0,0,1280,800));
}

WorkbenchNewTask WorkbenchWindow::NewTask() const { return {video_->text(),name_->text().trimmed(),root_->text().trimmed(),advanced_->text(),advanced_->text().isEmpty()?quality_->currentIndex():3}; }
void WorkbenchWindow::SetSuggestedTask(const WorkbenchNewTask& value) {
    if(state_.busy||state_.controlPending||state_.closing)return;
    video_->setText(value.video);name_->setText(value.name);root_->setText(value.root);
    {QSignalBlocker block(quality_);quality_->setCurrentIndex(std::clamp(value.profile,0,2));}
    advanced_->setText(value.profile==3?value.advancedConfig:QString());UpdateTarget();UpdateControls();
}
void WorkbenchWindow::ApplyState(const WorkbenchState& value) {
    state_=value; currentName_->setText(value.projectName.isEmpty()?QStringLiteral("准备好创作了吗？"):value.projectName); stateLabel_->setText(value.status.isEmpty()?QStringLiteral("选择左侧视频开始，或打开已有项目。"):value.status);
    stage_->setText(value.stage.isEmpty()?(value.busy?QStringLiteral("正在处理，等待阶段信息…"):QString()):(value.busy?QStringLiteral("正在处理 · %1"):QStringLiteral("阶段 · %1")).arg(value.stage));stage_->setToolTip(value.stage);stage_->setVisible(!value.stage.isEmpty()||value.busy);details_->setPlainText(value.detail);
    activeTime_->setText(QStringLiteral("活动耗时：%1").arg(Missing(value.activeTime)));completedAt_->setText(QStringLiteral("完成时间：%1").arg(Missing(value.completedAt)));trainingText_->setText(value.trainingText);
    training_->setVisible(value.trainingPercent>=0);if(value.trainingPercent>=0)training_->setValue(std::clamp(value.trainingPercent,0,100));trainingText_->setVisible(!value.trainingText.isEmpty());stageProgress_->setVisible(value.busy&&value.trainingPercent<0);
    environment_->setText(value.environment.section(QChar(':'),0,0).section(QChar(0xff1a),0,0));environment_->setToolTip(value.environment);environment_->setAccessibleDescription(value.environment);settingsEnvironment_->setText(value.environment);SetRecentProjects(recent_);UpdateControls();
}
void WorkbenchWindow::SetRecentProjects(const QList<WorkbenchProjectRow>& rows) {
    QString selectedPath,selectedExecution;
    if(auto* selected=projects_->currentItem()){selectedExecution=selected->data(0,Qt::UserRole+1).toString();while(selected->parent())selected=selected->parent();selectedPath=selected->data(0,Qt::UserRole).toString();}
    const int scroll=projects_->verticalScrollBar()->value();
    recent_=rows;projects_->clear();QTreeWidgetItem* current=nullptr;
    for(const auto& row:recent_){auto* item=new QTreeWidgetItem(projects_,{row.name,row.observedState,{},{}});item->setData(0,Qt::UserRole,row.path);item->setToolTip(0,row.path);if(QDir::cleanPath(row.path)==QDir::cleanPath(state_.projectPath))current=item;}
    if(!state_.projectPath.isEmpty()&&!current){current=new QTreeWidgetItem(projects_,{state_.projectName,state_.status,{},{}});current->setData(0,Qt::UserRole,state_.projectPath);current->setToolTip(0,state_.projectPath);}
    if(current){current->setText(1,state_.status);if(executionsProjectPath_==state_.projectPath)for(const auto& row:executions_){QString profile=row.profile;if(profile==QStringLiteral("Low"))profile=QStringLiteral("低");else if(profile==QStringLiteral("Medium"))profile=QStringLiteral("中");else if(profile==QStringLiteral("High"))profile=QStringLiteral("高");auto* child=new QTreeWidgetItem(current,{profile+QStringLiteral(" · ")+row.state,row.viewable?QStringLiteral("可查看"):QStringLiteral("无已验证结果"),Missing(row.completedAt),Missing(row.activeTime)});child->setData(0,Qt::UserRole+1,row.executionId);child->setData(0,Qt::UserRole+2,row.viewable);child->setToolTip(0,row.executionId);}current->setExpanded(true);}
    for(int i=0;i<projects_->topLevelItemCount();++i){auto* item=projects_->topLevelItem(i);if(item->data(0,Qt::UserRole).toString()!=selectedPath)continue;if(selectedExecution.isEmpty())projects_->setCurrentItem(item);else for(int j=0;j<item->childCount();++j)if(item->child(j)->data(0,Qt::UserRole+1).toString()==selectedExecution)projects_->setCurrentItem(item->child(j));}
    projects_->verticalScrollBar()->setValue(scroll);
    const bool empty=projects_->topLevelItemCount()==0;historyEmpty_->setVisible(empty);projects_->setVisible(!empty);remove_->setVisible(!empty);relocate_->setVisible(!empty);refresh_->setVisible(!state_.projectPath.isEmpty());
}
void WorkbenchWindow::SetExecutions(const QList<WorkbenchExecutionRow>& rows){executions_=rows;executionsProjectPath_=state_.projectPath;SetRecentProjects(recent_);}
void WorkbenchWindow::SetProjectRoot(const QString& root){root_->setText(root);settingsRoot_->setText(root);UpdateTarget();}
NativePreviewWidget* WorkbenchWindow::Preview() const{return preview_;}
void WorkbenchWindow::ShowWorkbench(){ExitFullscreen();pages_->setCurrentIndex(0);}
void WorkbenchWindow::ShowResult(const QString& name,const QString& detail){resultName_->setText(name);resultDetails_->setPlainText(detail);SetResultTiming({},{});pages_->setCurrentIndex(1);UpdateControls();}
void WorkbenchWindow::SetResultTiming(const QString& completedAt,const QString& activeTime){resultTimes_->setText(QStringLiteral("完成时间：%1    活动耗时：%2").arg(Missing(completedAt),Missing(activeTime)));}
void WorkbenchWindow::ShowSettings(){ExitFullscreen();pages_->setCurrentIndex(2);}
void WorkbenchWindow::ShowError(const QString& title,const QString& message){QMessageBox box(QMessageBox::Warning,title,QStringLiteral("操作未能完成，请检查以下说明。"),QMessageBox::Ok,this);box.setInformativeText(message.section('\n',0,0));box.setDetailedText(message);box.exec();}
void WorkbenchWindow::PermitClose(){closePermitted_=true;close();}
void WorkbenchWindow::closeEvent(QCloseEvent* event){if(closePermitted_){ExitFullscreen();event->accept();}else{event->ignore();emit closeRequested();}}
bool WorkbenchWindow::AcceptVideo(const QString& path) const {static const QStringList extensions={QStringLiteral("mp4"),QStringLiteral("mov"),QStringLiteral("mkv"),QStringLiteral("avi"),QStringLiteral("webm"),QStringLiteral("m4v"),QStringLiteral("mts"),QStringLiteral("m2ts")};return QFileInfo(path).isFile()&&extensions.contains(QFileInfo(path).suffix().toLower());}
void WorkbenchWindow::SelectVideo(const QString& path){if(state_.busy||state_.controlPending||state_.closing)return;if(!AcceptVideo(path)){ShowError(QStringLiteral("无法选择视频"),QStringLiteral("请选择一个本地视频文件。"));return;}emit videoSelected(path);}
void WorkbenchWindow::ChooseVideo(){const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("选择重建视频"),{},QStringLiteral("视频 (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mts *.m2ts);;所有文件 (*)"));if(!path.isEmpty())SelectVideo(path);}
void WorkbenchWindow::dragEnterEvent(QDragEnterEvent* event){if(!state_.busy&&!state_.controlPending&&!state_.closing&&event->mimeData()->hasUrls()){const auto urls=event->mimeData()->urls();if(urls.size()==1&&urls.front().isLocalFile()&&AcceptVideo(urls.front().toLocalFile()))event->acceptProposedAction();}}
void WorkbenchWindow::dropEvent(QDropEvent* event){const auto urls=event->mimeData()->urls();if(urls.size()==1&&urls.front().isLocalFile()&&!state_.busy&&!state_.controlPending&&!state_.closing){SelectVideo(urls.front().toLocalFile());event->acceptProposedAction();}}
void WorkbenchWindow::UpdateTarget(){const auto task=NewTask();const auto path=QDir::toNativeSeparators(QDir(task.root).filePath(task.name));target_->setText(task.name.isEmpty()?QStringLiteral("点击开始后才会创建项目并导入视频"):QStringLiteral("将创建项目：%1").arg(task.name));target_->setToolTip(task.name.isEmpty()?QString():path);target_->setAccessibleDescription(path);video_->setToolTip(task.video);root_->setToolTip(task.root);importHint_->setText(task.video.isEmpty()?QStringLiteral("拖入一段视频，开始创作"):QStringLiteral("已选择 · %1").arg(QFileInfo(task.video).fileName()));UpdateControls();}
void WorkbenchWindow::UpdateControls(){
    const bool editable=!state_.busy&&!state_.controlPending&&!state_.closing;
    for(auto* widget:QList<QWidget*>{video_,name_,root_,quality_,choose_,open_,advancedChoose_,rootChoose_,advanced_,projects_,refresh_,remove_,relocate_,runtime_,settingsRootChoose_})widget->setEnabled(editable);
    advancedGroup_->setEnabled(editable);
    start_->setEnabled(editable&&!state_.resumeAvailable&&!video_->text().isEmpty()&&!name_->text().trimmed().isEmpty()&&!root_->text().trimmed().isEmpty());
    start_->setText(state_.busy?QStringLiteral("重建进行中"):state_.resumeAvailable?QStringLiteral("请恢复原任务"):QStringLiteral("开始重建"));
    pause_->setEnabled(state_.pauseAvailable&&!state_.controlPending&&!state_.closing);resume_->setEnabled(state_.resumeAvailable&&editable);force_->setEnabled(state_.busy&&!state_.controlPending&&!state_.closing);
    pause_->setVisible(state_.busy&&state_.pauseAvailable);resume_->setVisible(state_.resumeAvailable);force_->setVisible(state_.busy);activeTime_->setVisible(!state_.projectPath.isEmpty()||state_.busy);completedAt_->setVisible(!state_.completedAt.isEmpty());
    const auto snapshot=preview_->Snapshot();reset_->setEnabled(snapshot.state==StudioPreviewState::Ready&&!state_.busy&&!state_.closing);retry_->setVisible(snapshot.state==StudioPreviewState::Failed);retry_->setEnabled(editable);
}
void WorkbenchWindow::resizeEvent(QResizeEvent* event){QMainWindow::resizeEvent(event);const auto orientation=width()<1120?Qt::Vertical:Qt::Horizontal;if(workColumns_->orientation()!=orientation){workColumns_->setOrientation(orientation);workColumns_->setSizes(orientation==Qt::Horizontal?QList<int>{420,630}:QList<int>{360,420});}}
void WorkbenchWindow::FitToAvailableGeometry(const QRect& available){
    if(!available.isValid())return;
    // Reserve logical space for the native frame/caption before creating the
    // preview owner. No native child or ancestor is reparented or recreated.
    const int side=std::max(8,(frameGeometry().width()-width())/2);
    const int top=std::max(40,frameGeometry().height()-height()-side);
    const QSize capacity(std::max(1,available.width()-2*side),std::max(1,available.height()-top-side));
    setMinimumSize(std::min(1024,capacity.width()),std::min(720,capacity.height()));
    resize(std::min(1280,capacity.width()),std::min(800,capacity.height()));
    // QWidget::move positions the top-level frame, not the client origin.
    // Keep the reserved native decoration inside the work area as well.
    const QSize outer(std::max(frameGeometry().width(),width()+2*side),std::max(frameGeometry().height(),height()+top+side));
    move(available.x()+std::max(0,(available.width()-outer.width())/2),available.y()+std::max(0,(available.height()-outer.height())/2));
}
void WorkbenchWindow::ApplyTheme(int preference){themePreference_=std::clamp(preference,0,2);{QSignalBlocker block(theme_);theme_->setCurrentIndex(themePreference_);}RefreshPalette();}
void WorkbenchWindow::RefreshPalette(){
    const bool dark=themePreference_==2||(themePreference_==0&&qApp->styleHints()->colorScheme()==Qt::ColorScheme::Dark);
    QPalette palette=qApp->style()->standardPalette();
    const QString bg=dark?QStringLiteral("#14171d"):QStringLiteral("#f4f6f9"),panel=dark?QStringLiteral("#20252e"):QStringLiteral("#ffffff"),nav=dark?QStringLiteral("#1b2028"):QStringLiteral("#eaeef4"),text=dark?QStringLiteral("#edf1f7"):QStringLiteral("#202b3a"),muted=dark?QStringLiteral("#aab5c6"):QStringLiteral("#626f82"),line=dark?QStringLiteral("#394250"):QStringLiteral("#dce3ec"),soft=dark?QStringLiteral("#233a59"):QStringLiteral("#eaf2ff"),blue=dark?QStringLiteral("#81b4ff"):QStringLiteral("#2468dd"),hover=dark?QStringLiteral("#2b3442"):QStringLiteral("#f0f4fa");
    palette.setColor(QPalette::Window,QColor(bg));palette.setColor(QPalette::WindowText,QColor(text));palette.setColor(QPalette::Base,QColor(panel));palette.setColor(QPalette::AlternateBase,QColor(bg));palette.setColor(QPalette::Text,QColor(text));palette.setColor(QPalette::Button,QColor(panel));palette.setColor(QPalette::ButtonText,QColor(text));palette.setColor(QPalette::Highlight,QColor(blue));palette.setColor(QPalette::HighlightedText,dark?QColor("#101b2c"):QColor(Qt::white));palette.setColor(QPalette::PlaceholderText,QColor(muted));palette.setColor(QPalette::Disabled,QPalette::Text,QColor(muted));palette.setColor(QPalette::Disabled,QPalette::ButtonText,QColor(muted));qApp->setPalette(palette);
    setStyleSheet(QStringLiteral(R"css(
QMainWindow { background:%1; color:%4; }
QLabel { background:transparent; color:%4; }
QLabel#pageTitle { font-size:26px; font-weight:600; }
QLabel#sectionTitle { font-size:17px; font-weight:600; }
QLabel#taskTitle { font-size:19px; font-weight:500; padding-top:6px; }
QLabel#support, QLabel#emptyState { color:%5; font-size:12px; }
QLabel#emptyState { padding:24px 8px; }
QLabel#brand { font-size:14px; font-weight:600; padding-top:8px; }
QLabel#brandMark { background:transparent; }
QLabel#importMark { color:%8; font-size:18px; font-weight:600; }
QLabel#stage { color:%8; font-size:14px; padding:6px 0; }
QWidget#navigation { background:%3; border:1px solid %6; border-radius:14px; }
QWidget#card { background:%2; border:1px solid %6; border-radius:14px; }
QWidget#importArea { background:%1; border:1px dashed %6; border-radius:12px; }
QPushButton { color:%4; background:%2; border:1px solid %6; border-radius:9px; padding:9px 12px; min-height:18px; }
QPushButton:hover { background:%9; border-color:%8; }
QPushButton:pressed { background:%7; }
QPushButton:focus, QLineEdit:focus, QComboBox:focus, QTreeWidget:focus { border:2px solid %8; }
QPushButton:disabled { color:%5; background:%1; border-color:%6; }
QPushButton#primary:enabled { background:#2468dd; color:white; border-color:#2468dd; font-weight:600; padding:12px; }
QPushButton#primary:enabled:hover { background:#1858c4; }
QPushButton#importButton { color:%8; background:%7; border-color:transparent; font-weight:600; }
QPushButton#quiet { background:transparent; border-color:transparent; color:%5; font-size:12px; padding:6px; }
QPushButton#quiet:hover { background:%9; color:%4; }
QPushButton#navButton { background:transparent; border-color:transparent; text-align:left; padding:11px 14px; }
QPushButton#navButton:checked { background:%7; color:%8; font-weight:600; }
QPushButton#navButton:hover { background:%9; }
QPushButton#navButton:focus, QPushButton#quiet:focus { border-color:%8; }
QLineEdit,QComboBox { color:%4; background:%1; border:1px solid %6; border-radius:8px; min-height:20px; padding:9px 10px; selection-background-color:%8; }
QLineEdit:disabled,QComboBox:disabled { color:%5; }
QComboBox::drop-down { border:0; width:26px; }
QComboBox QAbstractItemView { background:%2; color:%4; selection-background-color:%7; selection-color:%4; border:1px solid %6; }
QGroupBox#disclosure { border:0; border-top:1px solid %6; margin-top:14px; padding-top:12px; color:%5; font-size:12px; }
QGroupBox#disclosure::title { subcontrol-origin:margin; left:0; }
QPlainTextEdit { background:%1; color:%4; border:1px solid %6; border-radius:8px; padding:8px; font-size:12px; }
QTreeWidget { background:%2; color:%4; border:0; outline:0; }
QTreeWidget::item { min-height:24px; padding:8px 4px; border-bottom:1px solid %6; }
QTreeWidget::item:selected { background:%7; color:%4; border-radius:6px; }
QTreeWidget::item:hover { background:%9; }
QHeaderView::section { background:%2; color:%5; border:0; border-bottom:1px solid %6; padding:10px 4px; font-size:12px; }
QProgressBar { background:%1; color:%4; border:0; border-radius:4px; min-height:8px; max-height:18px; text-align:center; font-size:12px; }
QProgressBar::chunk { background:%8; border-radius:4px; }
QScrollArea,QScrollArea>QWidget>QWidget { background:transparent; border:0; }
QSplitter::handle { background:transparent; }
QScrollBar:vertical { background:transparent; width:10px; margin:2px; }
QScrollBar::handle:vertical { background:%6; min-height:32px; border-radius:4px; }
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }
QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical { background:transparent; }
QStatusBar { color:%5; font-size:12px; background:%1; padding:2px 12px; }
)css").arg(bg,panel,nav,text,muted,line,soft,blue,hover));
}
bool WorkbenchWindow::eventFilter(QObject* object,QEvent* event){
    if(event->type()==QEvent::KeyPress){
        auto* widget=qobject_cast<QWidget*>(object);auto* key=static_cast<QKeyEvent*>(event);
        if(widget&&(widget==this||isAncestorOf(widget))&&pages_->currentIndex()==1){
            const auto editingWithinWindow=[this](QWidget* candidate){
                if(!candidate||(candidate!=this&&!isAncestorOf(candidate)))return false;
                for(;candidate&&candidate!=this;candidate=candidate->parentWidget())
                    if(qobject_cast<QLineEdit*>(candidate)||qobject_cast<QPlainTextEdit*>(candidate)||qobject_cast<QComboBox*>(candidate))return true;
                return false;
            };
            // An editor can ignore F11/Esc and propagate it to an ancestor.
            // Preserve its input semantics throughout that propagation.
            const bool editing=editingWithinWindow(QApplication::focusWidget())||editingWithinWindow(widget);
            if(!editing&&key->modifiers()==Qt::NoModifier){if(key->key()==Qt::Key_F11){if(!key->isAutoRepeat())ToggleFullscreen();return true;}if(key->key()==Qt::Key_Escape&&fullscreen_){ExitFullscreen();return true;}}
        }
    }
    return QMainWindow::eventFilter(object,event);
}
void WorkbenchWindow::ToggleFullscreen(){
    if(fullscreen_){ExitFullscreen();return;}if(pages_->currentIndex()!=1)return;
    const HWND handle=reinterpret_cast<HWND>(winId());WINDOWPLACEMENT placement{};placement.length=sizeof(placement);MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
    if(!::GetWindowPlacement(handle,&placement)||!::GetMonitorInfoW(::MonitorFromWindow(handle,MONITOR_DEFAULTTONEAREST),&monitor)){ShowError(QStringLiteral("无法进入全屏"),QStringLiteral("无法读取窗口或显示器位置。"));return;}
    placement_=QByteArray(reinterpret_cast<const char*>(&placement),sizeof(placement));windowStyle_=::GetWindowLongPtrW(handle,GWL_STYLE);windowExStyle_=::GetWindowLongPtrW(handle,GWL_EXSTYLE);
    // In-place native style/geometry changes do not recreate Qt's borrowed HWND.
    ::SetLastError(0);const auto previous=::SetWindowLongPtrW(handle,GWL_STYLE,windowStyle_&~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW));
    if(!previous&&::GetLastError()){ShowError(QStringLiteral("无法进入全屏"),QStringLiteral("系统拒绝修改窗口样式。"));return;}
    const auto& r=monitor.rcMonitor;
    if(!::SetWindowPos(handle,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE)){::SetWindowLongPtrW(handle,GWL_STYLE,windowStyle_);::SetWindowPlacement(handle,&placement);ShowError(QStringLiteral("无法进入全屏"),QStringLiteral("系统拒绝调整窗口位置。"));return;}
    fullscreen_=true;nav_->hide();statusBar()->hide();fullscreenButton_->setText(QStringLiteral("退出全屏 (Esc)"));
}
void WorkbenchWindow::ExitFullscreen(){
    if(!fullscreen_)return;const HWND handle=reinterpret_cast<HWND>(winId());
    ::SetWindowLongPtrW(handle,GWL_STYLE,windowStyle_);::SetWindowLongPtrW(handle,GWL_EXSTYLE,windowExStyle_);
    WINDOWPLACEMENT placement{};if(placement_.size()==sizeof(placement)){std::memcpy(&placement,placement_.constData(),sizeof(placement));::SetWindowPlacement(handle,&placement);}
    ::SetWindowPos(handle,nullptr,0,0,0,0,SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);fullscreen_=false;nav_->show();statusBar()->show();fullscreenButton_->setText(QStringLiteral("全屏 (F11)"));
}
}

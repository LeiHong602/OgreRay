#include "Ogre\ExampleApplication.h"
// 中级教程二（射线）：RaySceneQuery 声明于此，用于射线场景查询（setRay / execute）
#include "Ogre\OgreSceneQuery.h"

#include "ogre\SdkTrays.h"
using namespace  OgreBites;


class MyBuffInputListener :public FrameListener, public OIS::MouseListener, public OIS::KeyListener, public SdkTrayListener
{
public:
    MyBuffInputListener(Ogre::SceneManager* pSM, RenderWindow* pWin, Ogre::Camera* cam)
        :mZoomSpeed(1.0f), mMouseSensitivity(0.15f),
        mRightMouseDown(false), mMiddleMouseDown(false), mShiftDown(false),
        mMouseDeltaX(0), mMouseDeltaY(0), mYaw(0), mPitch(0),
        mMoveDirection(Vector3::ZERO), m_ScaleTime(0), mAnimSpeed(1.0f),
        mBearRotX(0), mBearRotY(0), mBearRotZ(0)
    {
        m_Continue = true;
        translate = Ogre::Vector3(0, 0, 0);//定义移动向量

        if (pSM != NULL)
        {
            m_pSM = pSM;
        }

        m_cam = cam;

        size_t windowHnd = 0;
        std::stringstream windowHndStr;
        pWin->getCustomAttribute("WINDOW", &windowHnd);
        windowHndStr << windowHnd;
        OIS::ParamList pl;
        pl.insert(std::make_pair(std::string("WINDOW"), windowHndStr.str()));
        m_inputSystem = OIS::InputManager::createInputSystem(pl);
        m_key = static_cast<OIS::Keyboard*>(m_inputSystem->createInputObject(OIS::OISKeyboard, true));

        m_mouse = static_cast<OIS::Mouse*>(m_inputSystem->createInputObject(OIS::OISMouse, true));

        // ========== 相机节点 ==========
        mCameraNode = m_cam->getSceneManager()->getRootSceneNode()->createChildSceneNode("MainCameraNode");
        mCameraNode->attachObject(m_cam);
        mCameraNode->setOrientation(Quaternion(Degree(0), Vector3::UNIT_Y));

        //定义回调
        m_mouse->setEventCallback(this);
        m_key->setEventCallback(this);

        const OIS::MouseState& ms = m_mouse->getMouseState();
        ms.height = pWin->getHeight();
        ms.width = pWin->getWidth();

        m_ent = m_pSM->getEntity("MyEntity1");
        m_aniState = m_ent->getAnimationState("行走");

        newNode = m_pSM->getRootSceneNode()->createChildSceneNode();

        // ---------- 中级教程二：射线查询与地面拾取（原教程用地形 worldFragment，此处用平面求交） ----------
        // 向 SceneManager 申请一条“可反复设置”的射线查询对象；初始 Ray() 仅占位，真正射线在鼠标/每帧里 setRay
        mRaySceneQuery = m_pSM->createRayQuery(Ogre::Ray());
        // 查询结果按与射线起点距离排序，先命中近处物体（与官方示例习惯一致，便于以后扩展为打 Entity）
        mRaySceneQuery->setSortByDistance(true);
        // 左键在地面每生成一个 robot，用递增序号保证 Entity / SceneNode 名称唯一
        mCount = 0;
        // 左键是否处于按下状态：用于“按住左键拖动 = 拖动刚选中的物体沿地面滑动”（对应教程里拖动逻辑）
        mLMouseDown = false;
        // 指向“当前射线放置的那个物体”的根节点；每次左键点在地面会新建一个并更新此指针
        mCurrentObject = NULL;
        // 与 createScene() 里 createPlane(UNIT_Y, -10) 一致：无限大水平地面 y = -10，用于射线与地面对求交
        mGroundPlane = Ogre::Plane(Ogre::Vector3::UNIT_Y, -10.0f);

        m_TrayMgr = new SdkTrayManager("SampleControls", pWin, m_mouse, this);
        OnInitUI();
        m_TrayMgr->showCursor();
    }

    ~MyBuffInputListener()
    {
        // 谁创建谁销毁：RaySceneQuery 由 SceneManager 创建，必须在监听器析构时 destroyQuery，避免泄漏
        if (mRaySceneQuery)
            m_pSM->destroyQuery(mRaySceneQuery);
        delete m_TrayMgr;
        m_inputSystem->destroyInputObject(m_key);
        m_inputSystem->destroyInputObject(m_mouse);
        OIS::InputManager::destroyInputSystem(m_inputSystem);
    }

    void OnInitUI()
    {
        Ogre::FontManager::getSingleton().getByName("SdkTrays/Caption")->load();
        Ogre::FontManager::getSingleton().getByName("SdkTrays/Value")->load();

        // 文本（说明射线教程操作；原教程用 CEGUI，现改为 SDKTray，鼠标仍先 inject 给托盘再处理场景）
        OgreBites::TextBox* textBox;
        textBox = m_TrayMgr->createTextBox(TL_TOPLEFT, "Help", "RayHelp", 200, 100);
        textBox->setText("Tutorial2 Ray: L-click ground = spawn robot, drag = move. Tray UI eats clicks first.");

        //按钮
        OgreBites::Button* createButton;
        createButton = m_TrayMgr->createButton(TL_TOP, "CreateButton", "Create a new entity", 200);
        OgreBites::Button* deleteButton;
        deleteButton = m_TrayMgr->createButton(TL_TOP, "DeleteButton", "Delete the entity", 200);
        mSpeedButton = m_TrayMgr->createButton(TL_TOP, "SpeedButton", "Anim Speed: 1.0x", 200);

        //下拉框
        OgreBites::SelectMenu* selectMenu = m_TrayMgr->createThickSelectMenu(TL_TOPRIGHT, "BaoSelect", "Bao Select", 100, 100);
        Ogre::StringVector itemNames;
        itemNames.push_back("Stop");
        itemNames.push_back("Idle");
        itemNames.push_back("Move");
        itemNames.push_back("Attack");
        selectMenu->setItems(itemNames);

        //滑块
        OgreBites::Slider* bearMoveX = m_TrayMgr->createThickSlider(TL_BOTTOMLEFT, "BearMoveX", "BearMoveX", 300, 100, -100, 100, 100);
        OgreBites::Slider* bearMoveY = m_TrayMgr->createThickSlider(TL_BOTTOMLEFT, "BearMoveY", "BearMoveY", 300, 100, -100, 100, 100);
        OgreBites::Slider* bearMoveZ = m_TrayMgr->createThickSlider(TL_BOTTOMLEFT, "BearMoveZ", "BearMoveZ", 300, 100, -100, 100, 100);
        OgreBites::Slider* bearScale = m_TrayMgr->createThickSlider(TL_BOTTOMLEFT, "BearScale", "BearScale", 300, 100, 0.1, 5, 100);

        OgreBites::Slider* bearRotX = m_TrayMgr->createThickSlider(TL_BOTTOMRIGHT, "BearRotX", "BearRotX (deg)", 300, 100, -180, 180, 100);
        OgreBites::Slider* bearRotY = m_TrayMgr->createThickSlider(TL_BOTTOMRIGHT, "BearRotY", "BearRotY (deg)", 300, 100, -180, 180, 100);
        OgreBites::Slider* bearRotZ = m_TrayMgr->createThickSlider(TL_BOTTOMRIGHT, "BearRotZ", "BearRotZ (deg)", 300, 100, -180, 180, 100);
        bearRotX->setValue(0);
        bearRotY->setValue(0);
        bearRotZ->setValue(0);

        m_TrayMgr->showCursor();
    }

    /**
     * 从当前鼠标位置构造一条世界空间射线（替代原教程的 CEGUI::MouseCursor::getPosition）。
     * 原教程：mousePos.d_x / width、d_y / height 传入 getCameraToViewportRay。
     * 此处：用 OIS 的绝对像素坐标除以 width/height 得到 [0,1] 归一化视口坐标，再交给相机生成射线。
     * 注意：必须在 injectMouse* 判定“未点在 SDKTray 上”之后再调用，否则会与 UI 抢同一次点击。
     */
    Ogre::Ray getMouseRay(const OIS::MouseEvent& arg) const
    {
        Ogre::Real nx = Ogre::Real(arg.state.X.abs) / Ogre::Real(arg.state.width);
        Ogre::Real ny = Ogre::Real(arg.state.Y.abs) / Ogre::Real(arg.state.height);
        return m_cam->getCameraToViewportRay(nx, ny);
    }

    /**
     * 射线与“地面平面”求交。原教程用地形 RaySceneQuery 返回的 worldFragment->singleIntersection；
     * 本工程地面是无限平面（与 createScene 中 Plane(UNIT_Y,-10) 一致），用解析几何一次求交即可。
     * @param ray  一般为从相机指向屏幕像素的那条射线
     * @param out  输出：交点世界坐标（落在地面上）
     * @return     射线与平面平行（打不到）或反向时为 false
     */
    bool intersectGround(const Ogre::Ray& ray, Ogre::Vector3& out) const
    {
        // Ogre::Ray::intersects(Plane) 返回 (是否相交, 沿射线方向的参数 t)，再用 getPoint(t) 得到世界坐标点
        std::pair<bool, Ogre::Real> hit = ray.intersects(mGroundPlane);
        if (!hit.first)
            return false;
        out = ray.getPoint(hit.second);
        return true;
    }

    void buttonHit(Button* button) override
    {
        if (button->getName() == "CreateButton")
        {
            if (newNode->numAttachedObjects())
                return;
            Ogre::Entity* bear;
            if (m_pSM->hasEntity("bear"))//查看场景中是否已经有熊的实体了
            {
                bear = m_pSM->getEntity("bear");
            }
            else
            {
                bear = m_pSM->createEntity("bear", "熊.mesh");
            }
            newNode->setPosition(Ogre::Vector3(0, 100, 0));
            newNode->attachObject(bear);
            applyBearRotation();
        }
        if (button->getName() == "DeleteButton")
        {
            newNode->detachAllObjects();
        }
        if (button->getName() == "SpeedButton")
        {
            if (mAnimSpeed < 0.75f)
            {
                mAnimSpeed = 1.0f;
            }
            else if (mAnimSpeed < 1.25f)
            {
                mAnimSpeed = 1.5f;
            }
            else if (mAnimSpeed < 1.75f)
            {
                mAnimSpeed = 2.0f;
            }
            else
            {
                mAnimSpeed = 0.5f;
            }

            if (mSpeedButton)
            {
                mSpeedButton->setCaption("Anim Speed: " + Ogre::StringConverter::toString(mAnimSpeed) + "x");
            }
        }
    }

    void itemSelected(SelectMenu* menu) override
    {
        if (menu->getName() == "BaoSelect")
        {
            /*if (!newNode->numAttachedObjects())
            {
                return;
            }*/
            if (menu->getSelectedItem() == "Stop")
            {
                m_aniState->setEnabled(false);
                m_aniState->setLoop(false);
            }
            else if (menu->getSelectedItem() == "Idle")
            {
                m_aniState = m_ent->getAnimationState("站立");
                m_aniState->setEnabled(true);
                m_aniState->setLoop(true);
            }
            else if (menu->getSelectedItem() == "Move")
            {
                m_aniState = m_ent->getAnimationState("行走");
                m_aniState->setEnabled(true);
                m_aniState->setLoop(true);
            }
            else if (menu->getSelectedItem() == "Attack")
            {
                m_aniState = m_ent->getAnimationState("攻击");
                m_aniState->setEnabled(true);
                m_aniState->setLoop(true);
            }
        }
    }

    void sliderMoved(Slider* slider) override
    {
        if (slider->getName() == "BearMoveX")
        {
            if (!newNode->numAttachedObjects())
            {
                return;
            }
            newNode->setPosition(slider->getValue(), newNode->getPosition().y, newNode->getPosition().z);
        }
        else if (slider->getName() == "BearMoveY")
        {
            if (!newNode->numAttachedObjects())
            {
                return;
            }
            newNode->setPosition(newNode->getPosition().x, slider->getValue(), newNode->getPosition().z);
        }
        else if (slider->getName() == "BearMoveZ")
        {
            if (!newNode->numAttachedObjects())
            {
                return;
            }
            newNode->setPosition(newNode->getPosition().x, newNode->getPosition().y, slider->getValue());
        }
        else if (slider->getName() == "BearScale")
        {
            if (!newNode->numAttachedObjects())
            {
                return;
            }
            newNode->setScale(slider->getValue(), slider->getValue(), slider->getValue());
        }
        else if (slider->getName() == "BearRotX")
        {
            if (!newNode->numAttachedObjects())
            {
                return;
            }
            mBearRotX = slider->getValue();
            applyBearRotation();
        }
        else if (slider->getName() == "BearRotY")
        {
            if (!newNode->numAttachedObjects())
            {
                return;
            }
            mBearRotY = slider->getValue();
            applyBearRotation();
        }
        else if (slider->getName() == "BearRotZ")
        {
            if (!newNode->numAttachedObjects())
            {
                return;
            }
            mBearRotZ = slider->getValue();
            applyBearRotation();
        }
    }

    void applyBearRotation()
    {
        if (!newNode->numAttachedObjects())
            return;
        Quaternion q = Quaternion(Degree(mBearRotY), Vector3::UNIT_Y)
            * Quaternion(Degree(mBearRotX), Vector3::UNIT_X)
            * Quaternion(Degree(mBearRotZ), Vector3::UNIT_Z);
        newNode->setOrientation(q);
    }

    bool mouseMoved(const OIS::MouseEvent& arg) override
    {
        // 先让 UI 处理鼠标移动（包括滑块拖动、菜单悬停等）
        if (m_TrayMgr->injectMouseMove(arg))
            return true;

        // 教程：左键按住拖动时，把当前选中的物体沿地面移动到鼠标射线与地面交点（需已存在 mCurrentObject）
        if (mLMouseDown && mCurrentObject)
        {
            Ogre::Ray mouseRay = getMouseRay(arg);
            Ogre::Vector3 hit;
            if (intersectGround(mouseRay, hit))
            {
                // 节点位置取交点，使物体“贴地”滑动（仅平移，不改朝向）
                mCurrentObject->setPosition(hit);
            }
            // 拖动期间不再处理相机旋转/平移，避免与拾取冲突
            return true;
        }

        mMouseDeltaX = arg.state.X.rel * mMouseSensitivity;
        mMouseDeltaY = arg.state.Y.rel * mMouseSensitivity;

        if (mRightMouseDown)
            updateCameraRotation(arg);
        else if (mMiddleMouseDown)
            mCameraNode->translate(Vector3(-mMouseDeltaX * 0.8f, mMouseDeltaY * 0.8f, 0), Node::TS_LOCAL);

        if (arg.state.Z.rel != 0)
            updateCameraZoom(arg.state.Z.rel);

        return true;
    }

    bool mousePressed(const OIS::MouseEvent& arg, OIS::MouseButtonID id) override
    {
        // 先让 UI 处理点击（按钮、下拉框等）
        if (m_TrayMgr->injectMouseDown(arg, id))
            return true;

        // 左键：在地面交点处生成教程同款 robot（原教程用 CEGUI 取鼠标位置，此处用 OIS + getMouseRay）
        if (id == OIS::MB_Left)
        {
            Ogre::Ray mouseRay = getMouseRay(arg);
            Ogre::Vector3 hit;
            if (intersectGround(mouseRay, hit))
            {
                // 保留 RaySceneQuery 的用法：设置当前鼠标射线并执行查询（与教程一致；实际落点以平面求交为准）
                mRaySceneQuery->setRay(mouseRay);
                mRaySceneQuery->execute();

                // 实体名/节点名必须唯一，否则第二次 createEntity 会重名失败
                Ogre::String idStr = Ogre::StringConverter::toString(mCount++);
                Ogre::String entName = "PickRobot" + idStr;
                Ogre::String nodeName = "PickRobotNode" + idStr;

                // 资源需能在资源组中找到（通常为 Ogre 示例包里的 robot.mesh）
                Ogre::Entity* ent = m_pSM->createEntity(entName, "robot.mesh");
                mCurrentObject = m_pSM->getRootSceneNode()->createChildSceneNode(nodeName, hit);
                mCurrentObject->attachObject(ent);
                mCurrentObject->setScale(1.0f, 1.0f, 1.0f);
            }
            // 无论是否打到地面，都标记左键按下，与教程一致（未命中时无物体可拖）
            mLMouseDown = true;
        }

        if (id == OIS::MB_Right)  mRightMouseDown = true;
        if (id == OIS::MB_Middle) mMiddleMouseDown = true;
        return true;
    }

    bool mouseReleased(const OIS::MouseEvent& arg, OIS::MouseButtonID id) override
    {
        // 先让 UI 处理释放
        if (m_TrayMgr->injectMouseUp(arg, id))
            return true;

        // 左键释放后结束“拖动放置物”状态（mCurrentObject 仍指向最后一个生成的物体）
        if (id == OIS::MB_Left)
            mLMouseDown = false;

        if (id == OIS::MB_Right)  mRightMouseDown = false;
        if (id == OIS::MB_Middle) mMiddleMouseDown = false;
        return true;
    }

    bool keyPressed(const OIS::KeyEvent& arg) override
    {
        translate = Ogre::Vector3(0, 0, 0);

        switch (arg.key)
        {
        case OIS::KC_UP:
            translate += Ogre::Vector3(0, 0, -30);
            break;
        case OIS::KC_DOWN:
            translate += Ogre::Vector3(0, 0, 30);
            break;
        case OIS::KC_LEFT:
            translate += Ogre::Vector3(-30, 0, 0);
            break;
        case OIS::KC_RIGHT:
            translate += Ogre::Vector3(30, 0, 0);
            break;
        case OIS::KC_W:
            //m_cam->moveRelative(Ogre::Vector3(0, 0, -30) * evt.timeSinceLastFrame * 10);
        case OIS::KC_ESCAPE:
            m_Continue = false;
            break;
        case OIS::KC_Z:
            m_aniState = m_ent->getAnimationState("行走");
            m_aniState->setEnabled(true);
            m_aniState->setLoop(true);


        default:
            break;
        }



        return true;
    }
    bool keyReleased(const OIS::KeyEvent& arg) override
    {
        return true;
    }

    virtual bool frameStarted(const FrameEvent& evt)
    {
        if (!m_Continue)
        {
            return false;
        }

        m_TrayMgr->refreshCursor();
        m_TrayMgr->frameRenderingQueued(evt);

        // ---------- 教程 frameStarted：竖直向下射线防止相机“掉进地面以下” ----------
        // 原教程：从相机 (x,5000,z) 向下打 RaySceneQuery，用地形 worldFragment 高度抬升相机；
        // 此处：相机挂在 mCameraNode 上，用 getDerivedPosition 取世界坐标；向下射线与 mGroundPlane 求交得地面高度。
        {
            Ogre::Vector3 camWorld = m_cam->getDerivedPosition();
            // 起点足够高，保证从上方向下一定能与地面平面相交（与教程中 5000 同理）
            Ogre::Ray downRay(Ogre::Vector3(camWorld.x, 5000.0f, camWorld.z), Ogre::Vector3::NEGATIVE_UNIT_Y);
            mRaySceneQuery->setRay(downRay);
            mRaySceneQuery->execute();
            Ogre::Vector3 groundHit;
            if (intersectGround(downRay, groundHit))
            {
                Ogre::Real terrainHeight = groundHit.y;
                // 若眼睛高度低于“地面以上 10 单位”，则整体上移相机节点（保持 x、z，只修正高度差）
                if (camWorld.y < terrainHeight + 10.0f)
                {
                    Ogre::Vector3 lp = mCameraNode->getPosition();
                    mCameraNode->setPosition(lp.x, lp.y + (terrainHeight + 10.0f - camWorld.y), lp.z);
                }
            }
        }

        m_key->capture();
        m_mouse->capture();
        SceneNode* node1 = m_pSM->getSceneNode("bearNode");//获取要移动的节点
        node1->translate(translate * evt.timeSinceLastFrame);
        //translate = Ogre::Vector3(0, 0, 0);

        m_aniState->addTime(evt.timeSinceLastFrame * mAnimSpeed);//每帧的时间乘上动画的播放速度
        return true;
    }

    void updateCameraRotation(const OIS::MouseEvent& arg)
    {
        mYaw += Degree(-mMouseDeltaX);
        mPitch += Degree(-mMouseDeltaY);

        const Real maxPitch = 89.0f;
        if (mPitch.valueDegrees() > maxPitch) mPitch = Degree(maxPitch);
        if (mPitch.valueDegrees() < -maxPitch) mPitch = Degree(-maxPitch);

        Quaternion yawQuat(mYaw, Vector3::UNIT_Y);
        Quaternion pitchQuat(mPitch, Vector3::UNIT_X);
        mCameraNode->setOrientation(yawQuat * pitchQuat);
    }

    void updateCameraZoom(Real delta)
    {
        Real zoom = delta * mZoomSpeed;
        mCameraNode->translate(0, 0, -zoom, Node::TS_LOCAL);
    }

private:
    Ogre::SceneManager* m_pSM;
    OIS::Keyboard* m_key;
    OIS::InputManager* m_inputSystem;
    Ogre::Camera* m_cam;
    Ogre::SceneNode* mCameraNode;
    OIS::Mouse* m_mouse;
    bool m_isMouseRightClicked = false;
    Ogre::Vector3 translate;
    bool m_Continue;

    Ogre::Entity* m_ent;
    Ogre::AnimationState* m_aniState;

    SdkTrayManager* m_TrayMgr;//界面管理器

    Ogre::SceneNode* newNode;//新创建的节点
    OgreBites::Button* mSpeedButton;

    //鼠标移动相关
    Ogre::Real mMouseDeltaX, mMouseDeltaY;
    Ogre::Degree mYaw, mPitch;
    Ogre::Vector3 mMoveDirection;
    float m_ScaleTime;

    Ogre::Real mZoomSpeed;
    Ogre::Real mMouseSensitivity;
    Ogre::Real mAnimSpeed;
    bool mRightMouseDown, mMiddleMouseDown, mShiftDown;

    Ogre::Real mBearRotX, mBearRotY, mBearRotZ;

    // ---------- 中级教程二（射线）成员变量 ----------
    Ogre::RaySceneQuery* mRaySceneQuery; ///< 射线场景查询对象，配合 setRay/execute 使用（教程核心 API）
    bool mLMouseDown;                    ///< 左键是否按下，用于拖动当前放置物
    int mCount;                          ///< 已放置物体计数，生成唯一 Entity/节点名
    Ogre::SceneNode* mCurrentObject;     ///< 最近一次射线放置在地面上的物体根节点
    Ogre::Plane mGroundPlane;            ///< 与场景中地面网格一致的水平面，用于解析求交
};


class Example1 : public ExampleApplication
{
public:
    virtual bool configure(void)
    {
        if (!mRoot->showConfigDialog())//弹出ogre的配置对话框
            return false;

        Ogre::RenderSystem* rs = mRoot->getRenderSystem();//获取渲染系统
        if (rs != NULL)
            rs->setConfigOption("Video Mode", "1280 x 800 @ 32-bit colour");//设置视频模式

        mWindow = mRoot->initialise(true); //初始化窗口
        return true;
    }

    void createScene()
    {
        // 添加平面（与 MyBuffInputListener::mGroundPlane 中 Plane(UNIT_Y,-10) 保持一致，射线拾取才与地面一致）
        Ogre::Plane plane(Vector3::UNIT_Y, -10);
        Ogre::MeshManager::getSingleton().createPlane("plane",
            ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, plane,
            1500, 1500, 20, 20, true, 1, 5, 5, Vector3::UNIT_Z);//制造一个mesh
        Ogre::Entity* ent = mSceneMgr->createEntity("LightPlaneEntity", "plane");
        ent->setMaterialName("Examples/BeachStones");//设置平面的材质
        mSceneMgr->getRootSceneNode()->createChildSceneNode()->attachObject(ent);

        //创建豹子
        Ogre::Entity* bear = mSceneMgr->createEntity("MyEntity1", "豹.mesh");
        // 创建独立的子场景节点，避免重叠
        Ogre::SceneNode* bearNode = mSceneMgr->getRootSceneNode()->createChildSceneNode("bearNode");
        // 挂载实体到对应节点
        bearNode->attachObject(bear);


        //创建点光源
        Ogre::SceneNode* node = mSceneMgr->createSceneNode("Node1");
        mSceneMgr->getRootSceneNode()->addChild(node);
        Ogre::Light* light1 = mSceneMgr->createLight("Light1");
        light1->setType(Ogre::Light::LT_POINT);
        light1->setPosition(0, 200, 0);
        light1->setDiffuseColour(1.0f, 1.0f, 1.0f);

        //创建发光小球
        Ogre::Entity* LightEnt = mSceneMgr->createEntity("MyEntity", "sphere.mesh");
        Ogre::SceneNode* node3 = node->createChildSceneNode("node3");
        node3->setScale(0.1f, 0.1f, 0.1f);
        node3->setPosition(0, 200, 0);
        node3->attachObject(LightEnt);

        mSceneMgr->setShadowTechnique(Ogre::SHADOWTYPE_STENCIL_ADDITIVE);
    }

    void createCamera()
    {
        mCamera = mSceneMgr->createCamera("MyCamera0");
        mCamera->setPosition(0, 100, 400);
        mCamera->lookAt(0, 100, 0);
        mCamera->setNearClipDistance(5);
        //mCamera->setPolygonMode(Ogre::PM_WIREFRAME);

        mCamera1 = mSceneMgr->createCamera("MyCamera1");
        mCamera1->setPosition(1000, 2000, 0);
        mCamera1->lookAt(0, 0, 0);
        mCamera1->setNearClipDistance(5);

        mCamera2 = mSceneMgr->createCamera("MyCamera2");
        mCamera2->setPosition(1000, 0, 0);
        mCamera2->lookAt(0, 0, 0);
        mCamera2->setNearClipDistance(5);

        mCamera3 = mSceneMgr->createCamera("MyCamera3");
        mCamera3->setPosition(0, 0, 1000);
        mCamera3->lookAt(0, 0, 0);
        mCamera3->setNearClipDistance(5);
    }

    void createViewports()
    {
        Ogre::Viewport* vp = mWindow->addViewport(mCamera, 0, 0, 0, 1, 1);
        vp->setBackgroundColour(ColourValue(0.0f, 0.0f, 1.0f));
        mCamera->setAspectRatio(Real(vp->getActualWidth()) / Real(vp->getActualHeight()));

        //Ogre::Viewport* vp1 = mWindow->addViewport(mCamera1,1,0,0,0.5,0.5);
        //vp1->setBackgroundColour(ColourValue(0.0f, 1.0f, 0.0f));
        //mCamera->setAspectRatio(Real(1) / Real(1));

        //Ogre::Viewport* vp2 = mWindow->addViewport(mCamera2, 3, 0.5, 0, 0.5, 0.5);
        //vp2->setBackgroundColour(ColourValue(1.0f, 0.0f, 0.0f));
        //mCamera->setAspectRatio(Real(1) / Real(1));

        //Ogre::Viewport* vp3 = mWindow->addViewport(mCamera3, 4, 0, 0.5, 0.5, 0.5);
        //vp3->setBackgroundColour(ColourValue(1.0f, 1.0f, 0.0f));
        //mCamera->setAspectRatio(Real(1) / Real(1));
    }

    virtual void createFrameListener(void)
    {
        //MyListener* myListener = new MyListener(mSceneMgr);
        //mRoot->addFrameListener(myListener);

        //MyInputListener* myInputListener = new MyInputListener(mSceneMgr, mWindow, mCamera);
        //mRoot->addFrameListener(myInputListener);

        MyBuffInputListener* myBuffInputListener = new MyBuffInputListener(mSceneMgr, mWindow, mCamera);
        mRoot->addFrameListener(myBuffInputListener);

        //原本的帧监听
        //mFrameListener = new ExampleFrameListener(mWindow, mCamera);
        //mFrameListener->showDebugOverlay(true);
        //mRoot->addFrameListener(mFrameListener);

    }

private:
    Camera* mCamera1;
    Camera* mCamera2;
    Camera* mCamera3;

};

int main()
{
    Example1 app;
    app.go();

    return 0;
}
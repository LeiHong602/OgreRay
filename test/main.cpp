#include "Ogre\ExampleApplication.h"
#include "Ogre\OgreSceneQuery.h"

#include "ogre\SdkTrays.h"
#include <vector>

using namespace OgreBites;

/**
 * 本文件：中级教程二风格场景（terrain.cfg + 天空穹）+ SDKTray UI（Ogre 1.7）。
 *
 * 运行前请确认 resources.cfg / plugins.cfg 已包含：
 *   - 地形：含 terrain.cfg 的目录（SDK 示例常为 Samples/Media/Terrain）
 *   - 天空材质：Examples/CloudySky（Media/Examples 等）
 *   - 熊模型：熊.mesh（含「站立」「行走」骨骼动画名）
 *   - 路点标记：sphere.mesh
 *   - plugins.cfg 中需加载地形场景管理器插件（如 Plugin_OctreeSceneManager、地形相关插件，依 SDK 为准）
 *
 * 操作：鼠标左键点地形添加路点（顺序即路径）；熊沿折线循环行走；仅 1 个路点且到达后站立。
 * Clear all waypoints 清除路点与标记；速度按钮循环切换行走速度。
 */

class MyBuffInputListener : public FrameListener, public OIS::MouseListener, public OIS::KeyListener, public SdkTrayListener
{
public:
    MyBuffInputListener(Ogre::SceneManager* pSM, RenderWindow* pWin, Ogre::Camera* cam)
        : mZoomSpeed(1.0f), mMouseSensitivity(0.15f),
        mRightMouseDown(false), mMiddleMouseDown(false), mShiftDown(false),
        mMouseDeltaX(0), mMouseDeltaY(0), mYaw(0), mPitch(0),
        mAnimSpeed(1.0f),
        mTargetWaypointIndex(0),
        mBearMoveSpeed(80.0f),
        mWaypointReachDist(8.0f),
        mWaypointVisualCounter(0)
    {
        m_Continue = true;
        translate = Ogre::Vector3::ZERO;

        if (pSM != NULL)
            m_pSM = pSM;

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

        // 相机挂在节点上，便于与教程一致的“绕点观察”
        mCameraNode = m_cam->getSceneManager()->getRootSceneNode()->createChildSceneNode("MainCameraNode");
        mCameraNode->attachObject(m_cam);
        mCameraNode->setOrientation(Quaternion(Degree(0), Vector3::UNIT_Y));

        m_mouse->setEventCallback(this);
        m_key->setEventCallback(this);

        const OIS::MouseState& ms = m_mouse->getMouseState();
        ms.height = pWin->getHeight();
        ms.width = pWin->getWidth();

        // 主角色：场景里名为 MyEntity1 的熊实体（在 createScene 中创建）
        m_ent = m_pSM->getEntity("MyEntity1");
        mBearNode = m_pSM->getSceneNode("bearNode");

        // 动画：初始站立；具体启用关系由 setBearWalking() 维护
        m_aniWalk = m_ent->getAnimationState("行走");
        m_aniIdle = m_ent->getAnimationState("站立");
        if (m_aniWalk) { m_aniWalk->setLoop(true); }
        if (m_aniIdle) { m_aniIdle->setLoop(true); }
        setBearWalking(false);

        // 射线场景查询：地形拾取与相机贴地均依赖 worldFragment（与中级教程二一致）
        mRaySceneQuery = m_pSM->createRayQuery(Ogre::Ray());
        mRaySceneQuery->setSortByDistance(true);

        m_TrayMgr = new SdkTrayManager("SampleControls", pWin, m_mouse, this);
        OnInitUI();
        m_TrayMgr->showCursor();
    }

    ~MyBuffInputListener()
    {
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

        OgreBites::TextBox* textBox = m_TrayMgr->createTextBox(TL_TOPLEFT, "Help", "PathHelp", 260, 120);
        textBox->setText(
            "Terrain path: L-click = add waypoint. Bear follows path in order, loops. "
            "Clear = remove waypoints. Tray UI has priority.");

        m_TrayMgr->createButton(TL_TOP, "ClearWaypoints", "Clear all waypoints", 220);
        mSpeedButton = m_TrayMgr->createButton(TL_TOP, "SpeedButton", "Walk speed: 80", 220);
    }

    /** 屏幕像素 -> 相机射线（SDKTray 已 inject 之后再调用）。 */
    Ogre::Ray getMouseRay(const OIS::MouseEvent& arg) const
    {
        Ogre::Real nx = Ogre::Real(arg.state.X.abs) / Ogre::Real(arg.state.width);
        Ogre::Real ny = Ogre::Real(arg.state.Y.abs) / Ogre::Real(arg.state.height);
        return m_cam->getCameraToViewportRay(nx, ny);
    }

    /**
     * 沿射线与地形求交（Ogre 1.7 Terrain / world geometry 返回 worldFragment）。
     * 与教程 MouseQuery 中用法一致：execute() 后查找第一个带 worldFragment 的结果。
     */
    bool raycastTerrain(const Ogre::Ray& ray, Ogre::Vector3& outHit)
    {
        mRaySceneQuery->setRay(ray);
        Ogre::RaySceneQueryResult& res = mRaySceneQuery->execute();
        for (Ogre::RaySceneQueryResult::iterator itr = res.begin(); itr != res.end(); ++itr)
        {
            if (itr->worldFragment != NULL)
            {
                outHit = itr->worldFragment->singleIntersection;
                return true;
            }
        }
        return false;
    }

    /**
     * 在 (x,z) 处从高空竖直向下打射线，取地形表面高度（用于角色贴地）。
     */
    bool sampleTerrainHeight(Ogre::Real x, Ogre::Real z, Ogre::Real& outY)
    {
        Ogre::Ray downRay(Ogre::Vector3(x, 5000.0f, z), Ogre::Vector3::NEGATIVE_UNIT_Y);
        Ogre::Vector3 hit;
        if (!raycastTerrain(downRay, hit))
            return false;
        outY = hit.y;
        return true;
    }

    /**
     * 在「行走」与「站立」两套动画间切换；同时只播放一套，避免叠加。
     */
    void setBearWalking(bool walking)
    {
        if (m_aniWalk && m_aniIdle)
        {
            if (walking)
            {
                m_aniIdle->setEnabled(false);
                m_aniWalk->setEnabled(true);
                m_aniWalk->setLoop(true);
                m_aniState = m_aniWalk;
            }
            else
            {
                m_aniWalk->setEnabled(false);
                m_aniIdle->setEnabled(true);
                m_aniIdle->setLoop(true);
                m_aniState = m_aniIdle;
            }
        }
    }

    /** 清空路径数据并销毁路点可视化小球，避免 Entity/SceneNode 泄漏。 */
    void clearWaypoints()
    {
        for (size_t i = 0; i < mWaypointMarkers.size(); ++i)
        {
            Ogre::SceneNode* sn = mWaypointMarkers[i];
            if (!sn)
                continue;
            while (sn->numAttachedObjects() > 0)
            {
                Ogre::MovableObject* mo = sn->getAttachedObject(0);
                sn->detachObject((unsigned short)0);
                if (mo->getMovableType() == "Entity")
                    m_pSM->destroyEntity(static_cast<Ogre::Entity*>(mo));
            }
            m_pSM->destroySceneNode(sn);
        }
        mWaypointMarkers.clear();
        mWaypoints.clear();
        mTargetWaypointIndex = 0;
        setBearWalking(false);
    }

    void buttonHit(Button* button) override
    {
        if (button->getName() == "ClearWaypoints")
        {
            clearWaypoints();
        }
        else if (button->getName() == "SpeedButton")
        {
            if (mBearMoveSpeed < 60.0f)
                mBearMoveSpeed = 80.0f;
            else if (mBearMoveSpeed < 100.0f)
                mBearMoveSpeed = 120.0f;
            else
                mBearMoveSpeed = 40.0f;

            if (mSpeedButton)
                mSpeedButton->setCaption("Walk speed: " + Ogre::StringConverter::toString((int)mBearMoveSpeed));
        }
    }

    /** SdkTrayListener 要求实现；本示例已去掉滑块/下拉菜单改路点逻辑，故留空。 */
    void sliderMoved(Slider* slider) override
    {
        (void)slider;
    }

    void itemSelected(SelectMenu* menu) override
    {
        (void)menu;
    }

    bool mouseMoved(const OIS::MouseEvent& arg) override
    {
        if (m_TrayMgr->injectMouseMove(arg))
            return true;

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
        if (m_TrayMgr->injectMouseDown(arg, id))
            return true;

        // 左键：在「未点在托盘上」的前提下，用相机射线与地形求交，得到世界坐标 hit（贴地）
        if (id == OIS::MB_Left)
        {
            Ogre::Ray mouseRay = getMouseRay(arg);
            Ogre::Vector3 hit;
            if (raycastTerrain(mouseRay, hit))
            {
                // 顺序 push_back：路径为 路点0 -> 路点1 -> ... -> 回到路点0（环形）
                mWaypoints.push_back(hit);

                // 可视化：每个路点对应一个小球，便于观察折线顺序（与 clearWaypoints 成对销毁）
                Ogre::String idx = Ogre::StringConverter::toString(mWaypointVisualCounter++);
                Ogre::String entName = "WpEnt_" + idx;
                Ogre::String nodeName = "WpNode_" + idx;
                Ogre::Entity* ball = m_pSM->createEntity(entName, "sphere.mesh");
                Ogre::SceneNode* sn = m_pSM->getRootSceneNode()->createChildSceneNode(nodeName, hit);
                sn->setScale(0.15f, 0.15f, 0.15f);
                sn->attachObject(ball);
                mWaypointMarkers.push_back(sn);

                // 首次加入路点时，熊从当前位置开始朝 mWaypoints[0] 前进
                if (mWaypoints.size() == 1u)
                    mTargetWaypointIndex = 0;
            }
        }

        if (id == OIS::MB_Right)  mRightMouseDown = true;
        if (id == OIS::MB_Middle) mMiddleMouseDown = true;
        return true;
    }

    bool mouseReleased(const OIS::MouseEvent& arg, OIS::MouseButtonID id) override
    {
        if (m_TrayMgr->injectMouseUp(arg, id))
            return true;

        if (id == OIS::MB_Right)  mRightMouseDown = false;
        if (id == OIS::MB_Middle) mMiddleMouseDown = false;
        return true;
    }

    bool keyPressed(const OIS::KeyEvent& arg) override
    {
        translate = Ogre::Vector3::ZERO;

        switch (arg.key)
        {
        // 方向键：平移相机节点（世界空间），与熊的路径运动独立
        case OIS::KC_UP:    translate += Ogre::Vector3(0, 0, -30); break;
        case OIS::KC_DOWN:  translate += Ogre::Vector3(0, 0, 30); break;
        case OIS::KC_LEFT:  translate += Ogre::Vector3(-30, 0, 0); break;
        case OIS::KC_RIGHT: translate += Ogre::Vector3(30, 0, 0); break;
        case OIS::KC_ESCAPE: m_Continue = false; break;
        default: break;
        }
        return true;
    }

    bool keyReleased(const OIS::KeyEvent& arg) override
    {
        (void)arg;
        return true;
    }

    /**
     * 每帧：相机贴地（教程同款竖直射线）、熊沿折线路径移动并切换动画。
     */
    virtual bool frameStarted(const FrameEvent& evt)
    {
        if (!m_Continue)
            return false;

        m_TrayMgr->refreshCursor();
        m_TrayMgr->frameRenderingQueued(evt);

        // ---------- 相机防穿地：与教程 frameStarted 中 RaySceneQuery + worldFragment 一致 ----------
        {
            Ogre::Vector3 camPos = m_cam->getDerivedPosition();
            Ogre::Ray cameraRay(Ogre::Vector3(camPos.x, 5000.0f, camPos.z), Ogre::Vector3::NEGATIVE_UNIT_Y);
            mRaySceneQuery->setRay(cameraRay);
            Ogre::RaySceneQueryResult& result = mRaySceneQuery->execute();
            Ogre::RaySceneQueryResult::iterator itr = result.begin();
            if (itr != result.end() && itr->worldFragment != NULL)
            {
                Ogre::Real terrainHeight = itr->worldFragment->singleIntersection.y;
                if ((terrainHeight + 10.0f) > camPos.y)
                {
                    Ogre::Vector3 lp = mCameraNode->getPosition();
                    mCameraNode->setPosition(lp.x, lp.y + (terrainHeight + 10.0f - camPos.y), lp.z);
                }
            }
        }

        m_key->capture();
        m_mouse->capture();

        // 键盘平移相机节点（可选）
        mCameraNode->translate(translate * evt.timeSinceLastFrame, Ogre::Node::TS_WORLD);

        updateBearPathAndAnimation(evt.timeSinceLastFrame);

        if (m_aniState)
            m_aniState->addTime(evt.timeSinceLastFrame * mAnimSpeed);

        return true;
    }

    /**
     * 路点路径跟随：朝 mWaypoints[mTargetWaypointIndex] 水平移动，到达后索引循环递增。
     * 无路点时保持站立；移动时行走动画，并令熊朝向运动方向（仅绕 Y 轴）。
     */
    void updateBearPathAndAnimation(Ogre::Real dt)
    {
        if (!mBearNode || mWaypoints.empty())
        {
            setBearWalking(false);
            return;
        }

        Ogre::Vector3 pos = mBearNode->getPosition();
        Ogre::Vector3 target = mWaypoints[mTargetWaypointIndex];
        Ogre::Vector3 flat = target - pos;
        flat.y = 0;
        Ogre::Real dist = flat.length();

        // 到达当前目标：多路点时环形切换；仅有一个路点且已到位则一直「站立」
        if (dist <= mWaypointReachDist)
        {
            if (mWaypoints.size() <= 1)
            {
                setBearWalking(false);
                return;
            }
            mTargetWaypointIndex = (mTargetWaypointIndex + 1) % mWaypoints.size();
            return;
        }

        flat.normalise();
        Ogre::Vector3 move = flat * mBearMoveSpeed * dt;
        if (move.squaredLength() > dist * dist)
            move = flat * dist;

        Ogre::Vector3 newPos = pos + move;
        Ogre::Real h = newPos.y;
        if (sampleTerrainHeight(newPos.x, newPos.z, h))
            newPos.y = h + 2.0f;
        mBearNode->setPosition(newPos);

        // 水平面内朝向位移方向（模型前向为 -Z 时常用 atan2(-dx,-dz)）
        if (flat.squaredLength() > 1e-6f)
        {
            mBearNode->setOrientation(Ogre::Quaternion(Ogre::Math::ATan2(flat.x, flat.z), Ogre::Vector3::UNIT_Y));
        }

        setBearWalking(true);
    }

    void updateCameraRotation(const OIS::MouseEvent& arg)
    {
        (void)arg;
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
    Ogre::Vector3 translate;
    bool m_Continue;

    Ogre::Entity* m_ent;
    Ogre::AnimationState* m_aniState;
    Ogre::AnimationState* m_aniWalk;
    Ogre::AnimationState* m_aniIdle;
    Ogre::SceneNode* mBearNode;

    SdkTrayManager* m_TrayMgr;
    OgreBites::Button* mSpeedButton;

    Ogre::Real mMouseDeltaX, mMouseDeltaY;
    Ogre::Degree mYaw, mPitch;

    Ogre::Real mZoomSpeed;
    Ogre::Real mMouseSensitivity;
    Ogre::Real mAnimSpeed;
    bool mRightMouseDown, mMiddleMouseDown, mShiftDown;

    Ogre::RaySceneQuery* mRaySceneQuery;

    std::vector<Ogre::Vector3> mWaypoints;           ///< 用户点击顺序组成的路径（世界坐标，贴地）
    std::vector<Ogre::SceneNode*> mWaypointMarkers; ///< 各小球节点，与 mWaypoints 一一对应，便于清除
    size_t mTargetWaypointIndex;                    ///< 当前要走向的路点下标；到达后 (index+1)%N
    Ogre::Real mBearMoveSpeed;                      ///< 单位：世界单位/秒，沿水平方向移动
    Ogre::Real mWaypointReachDist;                  ///< 与目标路点水平距离小于此值视为到达
    int mWaypointVisualCounter;                     ///< 生成唯一 Entity/节点名用
};


class Example1 : public ExampleApplication
{
public:
    virtual bool configure(void)
    {
        if (!mRoot->showConfigDialog())
            return false;

        Ogre::RenderSystem* rs = mRoot->getRenderSystem();
        if (rs != NULL)
            rs->setConfigOption("Video Mode", "1280 x 800 @ 32-bit colour");

        mWindow = mRoot->initialise(true);
        return true;
    }

    /**
     * 使用室外封闭场景管理器以加载 terrain.cfg（与中级教程二一致）。
     * Ogre 1.7：ST_EXTERIOR_CLOSE 对应可 setWorldGeometry 的地形场景。
     */
    void chooseSceneManager(void)
    {
        mSceneMgr = mRoot->createSceneManager(Ogre::ST_EXTERIOR_CLOSE);
    }

    void createScene(void)
    {
        // 已删除原平面 Mesh；地面完全由 terrain.cfg 提供（与中级教程二 createScene 一致）

        // 环境光：避免背光面全黑
        mSceneMgr->setAmbientLight(ColourValue(0.5f, 0.5f, 0.5f));
        // 天空穹：第 2 参为材质名，后两个为球面细分参数（教程常用 5, 8）
        mSceneMgr->setSkyDome(true, "Examples/CloudySky", 5, 8);

        // 载入地形：会解析 terrain.cfg 中高度图、缩放、分页等（需在资源路径中能找到该文件）
        mSceneMgr->setWorldGeometry("terrain.cfg");

        // 熊：沿路径漫游的主体；动画名「站立」「行走」须与网格资源一致
        Ogre::Entity* bear = mSceneMgr->createEntity("MyEntity1", "熊.mesh");
        Ogre::SceneNode* bearNode = mSceneMgr->getRootSceneNode()->createChildSceneNode("bearNode");
        // 初始 (x,z) 取常见地形样例附近；y 由下方射线校正
        bearNode->setPosition(192, 0, 192);
        bearNode->attachObject(bear);

        // 初始高度贴地：从高空竖直向下 RaySceneQuery，取 worldFragment 交点 y + 少许偏移，防止穿模
        {
            Ogre::RaySceneQuery* rq = mSceneMgr->createRayQuery(Ogre::Ray());
            rq->setSortByDistance(true);
            Ogre::Vector3 p = bearNode->getPosition();
            Ogre::Ray down(Ogre::Vector3(p.x, 5000.0f, p.z), Ogre::Vector3::NEGATIVE_UNIT_Y);
            rq->setRay(down);
            Ogre::RaySceneQueryResult& res = rq->execute();
            for (Ogre::RaySceneQueryResult::iterator it = res.begin(); it != res.end(); ++it)
            {
                if (it->worldFragment)
                {
                    bearNode->setPosition(p.x, it->worldFragment->singleIntersection.y + 3.0f, p.z);
                    break;
                }
            }
            mSceneMgr->destroyQuery(rq);
        }

        Ogre::SceneNode* node = mSceneMgr->createSceneNode("Node1");
        mSceneMgr->getRootSceneNode()->addChild(node);
        Ogre::Light* light1 = mSceneMgr->createLight("Light1");
        light1->setType(Ogre::Light::LT_POINT);
        light1->setPosition(0, 200, 0);
        light1->setDiffuseColour(1.0f, 1.0f, 1.0f);

        Ogre::Entity* LightEnt = mSceneMgr->createEntity("MyEntity", "sphere.mesh");
        Ogre::SceneNode* node3 = node->createChildSceneNode("node3");
        node3->setScale(0.1f, 0.1f, 0.1f);
        node3->setPosition(0, 200, 0);
        node3->attachObject(LightEnt);

        mSceneMgr->setShadowTechnique(Ogre::SHADOWTYPE_STENCIL_ADDITIVE);
    }

    /**
     * 与教程相近的观察位置；相机仍由帧监听器里的节点驱动。
     */
    void createCamera(void)
    {
        mCamera = mSceneMgr->createCamera("MyCamera0");
        mCamera->setPosition(40, 100, 580);
        //mCamera->pitch(Degree(-30));
        //mCamera->yaw(Degree(-45));
        mCamera->setNearClipDistance(5);

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

    void createViewports(void)
    {
        Ogre::Viewport* vp = mWindow->addViewport(mCamera, 0, 0, 0, 1, 1);
        vp->setBackgroundColour(ColourValue(0.2f, 0.35f, 0.5f));
        mCamera->setAspectRatio(Real(vp->getActualWidth()) / Real(vp->getActualHeight()));
    }

    virtual void createFrameListener(void)
    {
        MyBuffInputListener* myBuffInputListener = new MyBuffInputListener(mSceneMgr, mWindow, mCamera);
        mRoot->addFrameListener(myBuffInputListener);
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

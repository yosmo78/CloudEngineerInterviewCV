#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN   // Exclude not needed stuff from Windows headers.
#endif

//nanogui Specific
#include <nanogui/opengl.h>
#include <nanogui/screen.h>
#include <nanogui/window.h>
#include <nanogui/layout.h>
#include <nanogui/label.h>
#include <nanogui/checkbox.h>
#include <nanogui/button.h>
#include <nanogui/toolbutton.h>
#include <nanogui/popupbutton.h>
#include <nanogui/combobox.h>
#include <nanogui/progressbar.h>
#include <nanogui/icons.h>
#include <nanogui/messagedialog.h>
#include <nanogui/textbox.h>
#include <nanogui/slider.h>
#include <nanogui/imagepanel.h>
#include <nanogui/imageview.h>
#include <nanogui/vscrollpanel.h>
#include <nanogui/colorwheel.h>
#include <nanogui/colorpicker.h>
#include <nanogui/graph.h>
#include <nanogui/tabwidget.h>
#include <nanogui/texture.h>
#include <nanogui/shader.h>
#include <nanogui/renderpass.h>

//OS Specific
#include <windows.h> //HAS TO COME AFTER NANOGUI INCLUDES

//C++ Specific
#include <iostream>
#include <chrono>
#include <string>
#include <random>
#include <atomic>
#include <memory>

//C Specific
#include <stdlib.h> //VC++'s C runtime is multithreaded by default. There's no need for rand_r, rand works fine in this case.
#include <stdint.h>
#include <time.h>

#define MSG_BUFFER_SIZE 500
#define NUM_PRODUCERS 1
#define NUM_PROGRESS_MONITORS 1

//array of all thread IDs
DWORD * threadIds;

//finished flag
HANDLE doneMutex;
std::atomic<bool> done = true;
std::atomic<bool> hasRun = false;

//sleeping condition variables
HANDLE producerSemaphore;
HANDLE senderSemaphore;

//msg buffer variables
HANDLE bufferMutex;

char msgBuffer[MSG_BUFFER_SIZE][101];

uint64_t msgsInBuffer = 0;
uint64_t startMsgIndex = 0;
uint64_t endMsgIndex = 0;


//output stats mutex
HANDLE statsMutex;
uint64_t msgsSent = 0;
uint64_t msgsFailed = 0;
int64_t sleepAmtAccum = 0;

const char* validPercentageRegex = "(100(\\.[0]*)?)|([0-9][0-9]?(\\.[0-9]*)?)";
const char* validDecimalNumberRegex = "[0-9][0-9]*(\\.[0-9]*)?";


typedef struct ProducerInfo
{
    uint64_t numMsgs = 1000;
} ProducerInfo;

typedef struct SenderInfo
{
    uint64_t meanMilliSecondWaitTime = 0; 
    double standardDevWaitTime = 1000.0; //default standard deviation is +- 1 sec
    double failureRate = 0.5;
} SenderInfo;

void usleep(int64_t usec);
inline  void GenRandomMsg100(char msgBuffer[]);
DWORD WINAPI Producer(LPVOID param);
inline void ProcessMsg(SenderInfo* sInfo, std::mt19937 & generator, std::normal_distribution<double> & waitTimeDistribution, std::uniform_real_distribution<double> & msgFailDistribution);
DWORD WINAPI Sender(LPVOID param);


using namespace nanogui;

class SMSAlertApplication : public Screen 
{
public:
    Widget* senderSettings;
    SMSAlertApplication() : Screen(Vector2i(1024, 768), "SMS Alert Simulation") {
        inc_ref();
        Window* window = new Window(this, "Run Simulation");
        window->set_position(Vector2i(250, 100));
        window->set_fixed_size(Vector2i(160, 300));
        window->set_layout(new GroupLayout());
        Button* b = new Button(window, "Run");
        b->set_callback([&] {
            DWORD res = WaitForSingleObject(doneMutex, 0L);
            switch(res)
            {
                case WAIT_TIMEOUT: break;
                default:
                {
                    if(done)
                    {
                        startSimulation();
                    }
                    ReleaseMutex(doneMutex);
                } break;
            }
            perform_layout();
        });
        new Label(window, "Progress bar", "sans-bold");
        m_progress = new ProgressBar(window);
        sentCount = new Label(window, "Sent: 0", "sans-bold");
        failCount = new Label(window, "Failed: 0", "sans-bold");
        avgTimePerMsg = new Label(window, "Avg Time: 0.0", "sans-bold"); 
        run_msg = new Label(window, "", "sans-bold");



        window = new Window(this, "Simulation Settings");
        window->set_position(Vector2i(425, 100));
        window->set_layout(new GroupLayout());
        window->set_fixed_size(Vector2i(350, 550));
        new Label(window, "Producer Message Count:", "sans-bold");
        producer_box = new IntBox<int>(window);
        producer_box->set_editable(true);
        producer_box->set_fixed_size(Vector2i(150, 20));
        producer_box->set_value(1000);
        producer_box->set_units(" msgs");
        producer_box->set_default_value("0");
        producer_box->set_font_size(16);
        producer_box->set_format("[0-9][0-9]*");
        producer_box->set_spinnable(true);
        producer_box->set_min_value(0);
        producer_box->set_value_increment(1);
        new Label(window, "Progress Monitor Update Time:", "sans-bold");
        progress_update_time = new TextBox(window);
        progress_update_time->set_editable(true);
        progress_update_time->set_fixed_size(Vector2i(100, 20));
        progress_update_time->set_value("0.0");
        progress_update_time->set_units("sec");
        progress_update_time->set_default_value("0.0");
        progress_update_time->set_font_size(16);
        progress_update_time->set_format(validDecimalNumberRegex);
        new Label(window, "Add/Remove Senders", "sans-bold");
        Widget* tools = new Widget(window);
        tools->set_layout(new BoxLayout(Orientation::Horizontal,
                                       Alignment::Middle, 0, 6));
        b = new Button(tools, "+");
        b->set_callback([&] {
            addSenderStat();
            perform_layout();
        });
        b = new Button(tools, "-");
        b->set_callback([&] {
            if(1 < senderSettings->child_count())
            {
                senderSettings->remove_child_at(senderSettings->child_count()-1);
                --numSenders;
            }
            perform_layout();
        });
        VScrollPanel* vscroll = new VScrollPanel(window);
        vscroll->set_fixed_size({300, 300});
        senderSettings = new Widget(vscroll);
        senderSettings->set_fixed_size({300, 300});
        senderSettings->set_layout(new GridLayout(Orientation::Horizontal, 1,
                             Alignment::Minimum, 5, 0));
        addSenderStat();

        

        perform_layout();

        m_render_pass = new RenderPass({ this });
        m_render_pass->set_clear_color(0, Color(0.3f, 0.3f, 0.32f, 1.f));

        bufferMutex = CreateMutexA(NULL,                //default security attributes
                                   false,               //does thread calling it auto get the mutex
                                   "Msg Buffer Mutex"); //mutex name

        statsMutex = CreateMutexA(NULL,                 //default security attributes
                                  false,                //does thread calling it auto get the mutex
                                  "Stats Mutex");       //mutex name

        doneMutex = CreateMutexA(NULL,                  //default security attributes
                                false,                  //does thread calling it auto get the mutex
                                "Done Mutex");          //mutex name

        start = std::chrono::system_clock::now();

    }

    void startSimulation()
    {
        if(hasRun)
        {
            DWORD res = WaitForMultipleObjects(threadHandles.size(), &threadHandles[0], true, 0L);
            switch(res)
            {
                case WAIT_TIMEOUT:
                {
                    run_msg->set_caption("Still Running");
                    return;
                } 
                default: break;
            }
            CloseHandle(producerSemaphore);
            CloseHandle(senderSemaphore);
            delete [] threadIds;
            if(pInfo != nullptr)
                delete pInfo;
            if(sInfo != nullptr)
                delete [] sInfo;
        }
        producerSemaphore = CreateSemaphoreA(NULL,
                                             500,                //init count
                                             500,                //max count
                                             "Producer Sema");

        senderSemaphore =   CreateSemaphoreA(NULL,
                                             0,                  //init count
                                             500,                //max count
                                             "Sender Sema");
        done = false;
        pInfo = new ProducerInfo();
        pInfo->numMsgs = producer_box->value();
        totalNumMsgs = pInfo->numMsgs;

        updateInterval = std::stod(progress_update_time->value())*1000.0;
        threadIds = new DWORD[NUM_PRODUCERS+numSenders];
        WaitForSingleObject(bufferMutex, INFINITE);
        WaitForSingleObject(statsMutex, INFINITE);
        msgsInBuffer = 0;
        startMsgIndex = 0;
        endMsgIndex = 0;
        msgsSent = 0;
        msgsFailed = 0;
        sleepAmtAccum = 0;
        threadHandles.clear();

        //Create Produce thread
        threadHandles.push_back(CreateThread(NULL,               // default security attributes
                                             0,                  // use default stack size  
                                             Producer,           // thread function name
                                             pInfo,              // argument to thread function 
                                             0,                  // use default creation flags 
                                             threadIds));

        //Allow each Sender to be customizable
        sInfo = new SenderInfo[numSenders];

        const std::vector<Widget*> & childs = senderSettings->children();
        for(int i = 0; i < childs.size(); ++i)
        {
            sInfo[i].meanMilliSecondWaitTime = (uint64_t)(std::stod(reinterpret_cast<TextBox*>(childs[i]->child_at(3))->value())*1000.0);
            sInfo[i].standardDevWaitTime = std::stod(reinterpret_cast<TextBox*>(childs[i]->child_at(5))->value())*1000.0;
            sInfo[i].failureRate = std::stod(reinterpret_cast<TextBox*>(childs[i]->child_at(7))->value())/100.0;
        }


        //Create Senders (Consumers) thread
        for(int i = 0; i < numSenders; ++i)
        {
            threadHandles.push_back(CreateThread(NULL,               // default security attributes
                                                 0,                  // use default stack size  
                                                 Sender,             // thread function name
                                                 &sInfo[i],          // argument to thread function 
                                                 0,                  // use default creation flags 
                                                 threadIds+NUM_PRODUCERS+i)); 
        }

        ReleaseMutex(bufferMutex);  
        ReleaseMutex(statsMutex);
        run_msg->set_caption("Running");
    }   

    void addSenderStat()
    {
        ++numSenders;
        Widget * senderStats = new Widget(senderSettings);
        GridLayout * l = new GridLayout(Orientation::Horizontal, 2,
                             Alignment::Minimum, 0, 0);
        l->set_col_alignment(
        { Alignment::Minimum, Alignment::Minimum });
        l->set_spacing(0, 10);


        senderStats->set_layout(l);
        new Label(senderStats, (std::string("Sender ")+std::to_string(numSenders)+std::string(":")).c_str(), "sans-bold");
        new Label(senderStats, "", "sans-bold");
        new Label(senderStats, "Mean Wait Time :", "sans-bold");
        auto text_box = new TextBox(senderStats);
        text_box->set_editable(true);
        text_box->set_fixed_size(Vector2i(100, 20));
        text_box->set_value("0.0");
        text_box->set_units("sec");
        text_box->set_default_value("0.0");
        text_box->set_font_size(16);
        text_box->set_format(validDecimalNumberRegex);
        new Label(senderStats, "Standard Time Deviation :", "sans-bold");
        text_box = new TextBox(senderStats);
        text_box->set_editable(true);
        text_box->set_fixed_size(Vector2i(100, 20));
        text_box->set_value("1.0");
        text_box->set_units("sec");
        text_box->set_default_value("1.0");
        text_box->set_font_size(16);
        text_box->set_format(validDecimalNumberRegex);
        new Label(senderStats, "Failure Rate :", "sans-bold");
        text_box = new TextBox(senderStats);
        text_box->set_editable(true);
        text_box->set_fixed_size(Vector2i(100, 20));
        text_box->set_value("0.0");
        text_box->set_units("%");
        text_box->set_default_value("0.0");
        text_box->set_font_size(16);
        text_box->set_format(validPercentageRegex);
    }

    virtual bool keyboard_event(int key, int scancode, int action, int modifiers) {
        if (Screen::keyboard_event(key, scancode, action, modifiers))
            return true;
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            set_visible(false);
            return true;
        }
        return false;
    }

    virtual void draw(NVGcontext *ctx) {
        if(std::chrono::duration<double, std::ratio<1, 1000>>(std::chrono::system_clock::now()-start).count() > updateInterval)
        {
            WaitForSingleObject(statsMutex, INFINITE);
            int tot = msgsSent+msgsFailed;
            if(tot != 0)
            {
                m_progress->set_value(tot / (double) (totalNumMsgs+1)); 
                sentCount->set_caption((std::string("Sent: ")+std::to_string(msgsSent)).c_str());
                failCount->set_caption((std::string("Failed: ")+std::to_string(msgsFailed)).c_str());
                avgTimePerMsg->set_caption((std::string("Avg Time: ")+std::to_string(sleepAmtAccum/1000.0/tot)).c_str());
                if(tot == totalNumMsgs)
                {
                    run_msg->set_caption("Done");
                }
                perform_layout();
            }
            ReleaseMutex(statsMutex);
            start = std::chrono::system_clock::now();
        }

        /* Draw the user interface */
        Screen::draw(ctx);
    }

    virtual void draw_contents() {
        m_render_pass->resize(framebuffer_size());
        m_render_pass->begin();
        m_render_pass->end();
    }
private:
    IntBox<int>* producer_box;
    TextBox* progress_update_time;
    int numSenders = 0;
    std::vector<HANDLE> threadHandles;
    ProducerInfo* pInfo = nullptr;
    SenderInfo* sInfo = nullptr;
    ProgressBar *m_progress;
    ref<RenderPass> m_render_pass;
    uint64_t totalNumMsgs = 1;
    double updateInterval;
    Label* run_msg;
    Label* sentCount;
    Label* failCount;
    Label* avgTimePerMsg;
    std::chrono::time_point<std::chrono::system_clock> start;
};


inline 
void GenRandomMsg100(char msgBuffer[])
{
    for(int i = 0; i < 100; ++i)
    {
        //range of good chars 127 - 32 + 1 = 96. It starts at 32 so + 32
        msgBuffer[i] = (char) (rand()%96+32); 
    }
    msgBuffer[100] = '\0';
}

DWORD WINAPI Producer(LPVOID param) 
{ 
    ProducerInfo* pInfo = (ProducerInfo*) param;
    for(int msg = 0; msg < pInfo->numMsgs; ++msg)
    {
        WaitForSingleObject(producerSemaphore,INFINITE);

        WaitForSingleObject(bufferMutex, INFINITE);
        GenRandomMsg100(msgBuffer[endMsgIndex]);
        endMsgIndex = (endMsgIndex+1) % MSG_BUFFER_SIZE;
        ++msgsInBuffer;
        ReleaseMutex(bufferMutex);
        ReleaseSemaphore(senderSemaphore, 1, NULL);
    }
    WaitForSingleObject(doneMutex, INFINITE);
    done = true;
    hasRun = true;
    ReleaseMutex(doneMutex);
    return 0;
}

inline
void ProcessMsg(SenderInfo* sInfo, std::mt19937 & generator, std::normal_distribution<double> & waitTimeDistribution, std::uniform_real_distribution<double> & msgFailDistribution)
{
    int64_t sleepAmt = waitTimeDistribution(generator);
    if(sleepAmt < 0.0)
        sleepAmt = 0.0;
    usleep(sleepAmt);
    bool failed = (msgFailDistribution(generator) < sInfo->failureRate);
    WaitForSingleObject(statsMutex, INFINITE);
    sleepAmtAccum += sleepAmt;
    if(failed)
    {
        ++msgsFailed;
    }
    else
    {
        ++msgsSent;
    }
    ReleaseMutex(statsMutex);
}

DWORD WINAPI Sender(LPVOID param) 
{ 
    char msg[101];
    msg[100] ='\0';
    SenderInfo* sInfo = (SenderInfo*) param;
    static thread_local std::mt19937 generator(time(NULL));
    std::normal_distribution<double> waitTimeDistribution(sInfo->meanMilliSecondWaitTime,sInfo->standardDevWaitTime);
    std::uniform_real_distribution<double> msgFailDistribution(0.0,1.0);
    while(!done)
    {
        DWORD waitResult = WaitForSingleObject(senderSemaphore,0L);
        switch(waitResult)
        {
            case WAIT_TIMEOUT:
            {
                continue;
            }
            default:
                break;
        }

        WaitForSingleObject(bufferMutex, INFINITE);
        memcpy(&msg[0], &msgBuffer[startMsgIndex][0], 100);
        startMsgIndex = (startMsgIndex+1) % MSG_BUFFER_SIZE;
        --msgsInBuffer;


        ReleaseMutex(bufferMutex);

        ReleaseSemaphore(producerSemaphore, 1, NULL);

        ProcessMsg(sInfo, generator, waitTimeDistribution, msgFailDistribution);
    }
    //clean up loop, no sync with producer needed here
    WaitForSingleObject(bufferMutex, INFINITE);
    while(msgsInBuffer != 0)
    {

        memcpy(&msg[0], &msgBuffer[startMsgIndex][0], 100);
        startMsgIndex = (startMsgIndex+1) % MSG_BUFFER_SIZE;
        --msgsInBuffer;


        ReleaseMutex(bufferMutex);

        ProcessMsg(sInfo, generator, waitTimeDistribution, msgFailDistribution);

        WaitForSingleObject(bufferMutex, INFINITE);
    }
    ReleaseMutex(bufferMutex);
    return 0;
}

//I did not implement this usleep, got it from https://www.c-plusplus.net/forum/topic/109539/usleep-unter-windows/3
void usleep(int64_t usec) 
{ 
    HANDLE timer; 
    LARGE_INTEGER ft; 

    ft.QuadPart = -(10*usec); // Convert to 100 nanosecond interval, negative value indicates relative time

    timer = CreateWaitableTimer(NULL, TRUE, NULL); 
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0); 
    WaitForSingleObject(timer, INFINITE); 
    CloseHandle(timer); 
}
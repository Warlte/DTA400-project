#include "ns3/core-module.h"
#include "ns3/random-variable-stream.h"
#include <iostream>
#include <vector>
#include <map>
#include <queue>
#include <iomanip>
#include <limits>
#include <fstream>
#include <sstream>
#include <algorithm>


using namespace ns3;


NS_LOG_COMPONENT_DEFINE("SupermarketSimulation");


std::map<uint32_t, std::vector<double>> waitingTimes;


struct CashierResults
{
    uint32_t numCashiers;
    uint32_t totalCustomers;
    double avgWaitingTime;
    double utilization;
    double efficiencyScore;
};


std::vector<CashierResults> allResults;


class Customer : public Object
{
public:
    Customer(uint32_t id, double arrivalTime);
    virtual ~Customer();
   
    uint32_t GetId() const { return m_id; }
    double GetArrivalTime() const { return m_arrivalTime; }
    double GetServiceStartTime() const { return m_serviceStartTime; }
    double GetServiceEndTime() const { return m_serviceEndTime; }
    double GetWaitingTime() const { return m_serviceStartTime - m_arrivalTime; }
    double GetServiceTime() const { return m_serviceEndTime - m_serviceStartTime; }
   
    void SetServiceStartTime(double time) { m_serviceStartTime = time; }
    void SetServiceEndTime(double time) { m_serviceEndTime = time; }
   
    static TypeId GetTypeId(void);
   
private:
    uint32_t m_id;
    double m_arrivalTime;
    double m_serviceStartTime;
    double m_serviceEndTime;
};


Customer::Customer(uint32_t id, double arrivalTime)
    : m_id(id), m_arrivalTime(arrivalTime), m_serviceStartTime(0), m_serviceEndTime(0)
{
}


Customer::~Customer()
{
}


TypeId Customer::GetTypeId(void)
{
    static TypeId tid = TypeId("Customer")
        .SetParent<Object>()
        .SetGroupName("Supermarket");
    return tid;
}


class Cashier : public Object
{
public:
    Cashier(uint32_t id);
    virtual ~Cashier();
   
    bool IsBusy() const { return m_busy; }
    void StartService(Ptr<Customer> customer, double currentTime);
    Ptr<Customer> EndService(double currentTime);
    Ptr<Customer> GetCurrentCustomer() const { return m_currentCustomer; }
    double GetTotalServiceTime() const { return m_totalServiceTime; }
    double GetTotalIdleTime() const { return m_totalIdleTime; }
    double GetLastIdleTime() const { return m_lastIdleTime; }
    void FinalizeIdleTime(double currentTime);
   
    static TypeId GetTypeId(void);
   
private:
    uint32_t m_id;
    bool m_busy;
    Ptr<Customer> m_currentCustomer;
    double m_totalServiceTime;
    double m_totalIdleTime;
    double m_lastIdleTime;
    double m_lastActivityTime;
};


Cashier::Cashier(uint32_t id)
    : m_id(id), m_busy(false), m_currentCustomer(nullptr), m_totalServiceTime(0),
      m_totalIdleTime(0), m_lastIdleTime(0), m_lastActivityTime(0)
{
}


Cashier::~Cashier()
{
}


void Cashier::StartService(Ptr<Customer> customer, double currentTime)
{
    if (m_busy)
    {
        NS_LOG_ERROR("Cashier " << m_id << " is already busy!");
        return;
    }
   
    if (!m_busy && m_lastActivityTime > 0)
    {
        m_totalIdleTime += (currentTime - m_lastActivityTime);
        m_lastIdleTime = (currentTime - m_lastActivityTime);
    }
    else if (m_lastActivityTime == 0)
    {
        m_totalIdleTime += currentTime;
        m_lastIdleTime = currentTime;
    }
   
    m_busy = true;
    m_currentCustomer = customer;
    customer->SetServiceStartTime(currentTime);
    m_lastActivityTime = currentTime;
}


Ptr<Customer> Cashier::EndService(double currentTime)
{
    if (!m_busy)
    {
        NS_LOG_ERROR("Cashier " << m_id << " is not busy!");
        return nullptr;
    }
   
    if (m_currentCustomer == nullptr)
    {
        NS_LOG_ERROR("Cashier " << m_id << " has null customer pointer!");
        m_busy = false;
        return nullptr;
    }
   
    double serviceTime = currentTime - m_currentCustomer->GetServiceStartTime();
    m_totalServiceTime += serviceTime;
    m_currentCustomer->SetServiceEndTime(currentTime);
    Ptr<Customer> completedCustomer = m_currentCustomer;
    m_busy = false;
    m_currentCustomer = nullptr;
    m_lastActivityTime = currentTime;
    return completedCustomer;
}


void Cashier::FinalizeIdleTime(double currentTime)
{
    if (!m_busy && m_lastActivityTime > 0)
    {
        m_totalIdleTime += (currentTime - m_lastActivityTime);
    }
    else if (m_lastActivityTime == 0)
    {
        m_totalIdleTime = currentTime;
    }
}


TypeId Cashier::GetTypeId(void)
{
    static TypeId tid = TypeId("Cashier")
        .SetParent<Object>()
        .SetGroupName("Supermarket");
    return tid;
}


class SupermarketSimulation
{
public:
    SupermarketSimulation(uint32_t numCashiers, double arrivalRate, double serviceRate);
    void RunSimulation(double simulationTime);
    void PrintResults();
   
private:
    void CustomerArrival();
    void CustomerServiceEnd(uint32_t cashierId);
    void ScheduleNextArrival();
    void ScheduleServiceEnd(uint32_t cashierId, double serviceTime);
    void StopSimulation();
   
    uint32_t m_numCashiers;
    double m_arrivalRate;
    double m_serviceRate;
    double m_simulationTime;
    uint32_t m_customerId;
   
    std::vector<Ptr<Cashier>> m_cashiers;
    std::queue<Ptr<Customer>> m_queue;
    std::vector<Ptr<Customer>> m_completedCustomers;
   
    Ptr<ExponentialRandomVariable> m_arrivalRandom;
    Ptr<ExponentialRandomVariable> m_serviceRandom;
   
    EventId m_nextArrivalEvent;
    std::vector<EventId> m_serviceEndEvents;
    bool m_stopped;
};


SupermarketSimulation::SupermarketSimulation(uint32_t numCashiers, double arrivalRate, double serviceRate)
    : m_numCashiers(numCashiers), m_arrivalRate(arrivalRate), m_serviceRate(serviceRate),
      m_simulationTime(0), m_customerId(0), m_stopped(false)
{
    for (uint32_t i = 0; i < m_numCashiers; i++)
    {
        Ptr<Cashier> cashier = CreateObject<Cashier>(i);
        m_cashiers.push_back(cashier);
    }
   
    m_arrivalRandom = CreateObject<ExponentialRandomVariable>();
    m_arrivalRandom->SetAttribute("Mean", DoubleValue(1.0 / m_arrivalRate));
   
    m_serviceRandom = CreateObject<ExponentialRandomVariable>();
    m_serviceRandom->SetAttribute("Mean", DoubleValue(1.0 / m_serviceRate));
   
    m_serviceEndEvents.resize(m_numCashiers);
}


void SupermarketSimulation::RunSimulation(double simulationTime)
{
    NS_LOG_INFO("Starting simulation with " << m_numCashiers << " cashiers");
    NS_LOG_INFO("Arrival rate: " << m_arrivalRate << " customers/second");
    NS_LOG_INFO("Service rate: " << m_serviceRate << " customers/second");
    NS_LOG_INFO("Simulation time: " << simulationTime << " seconds");
   
    m_simulationTime = simulationTime;
    m_stopped = false;
   
    ScheduleNextArrival();
   
    Simulator::Schedule(Seconds(simulationTime), &SupermarketSimulation::StopSimulation, this);
   
    Simulator::Run();
   
    double currentTime = Simulator::Now().GetSeconds();
    for (uint32_t i = 0; i < m_numCashiers; i++)
    {
        if (m_cashiers[i]->IsBusy())
        {
            Ptr<Customer> customer = m_cashiers[i]->EndService(currentTime);
            if (customer != nullptr)
            {
                m_completedCustomers.push_back(customer);
                waitingTimes[m_numCashiers].push_back(customer->GetWaitingTime());
            }
        }
        else
        {
            m_cashiers[i]->FinalizeIdleTime(currentTime);
        }
    }
}


void SupermarketSimulation::StopSimulation()
{
    m_stopped = true;
    if (!m_nextArrivalEvent.IsExpired())
    {
        Simulator::Cancel(m_nextArrivalEvent);
    }
    for (uint32_t i = 0; i < m_numCashiers; i++)
    {
        if (!m_serviceEndEvents[i].IsExpired())
        {
            Simulator::Cancel(m_serviceEndEvents[i]);
        }
    }
    Simulator::Stop();
}


void SupermarketSimulation::CustomerArrival()
{
    if (m_stopped)
    {
        return;
    }
   
    double currentTime = Simulator::Now().GetSeconds();
   
    Ptr<Customer> customer = CreateObject<Customer>(m_customerId++, currentTime);
   
    bool assigned = false;
    for (uint32_t i = 0; i < m_numCashiers; i++)
    {
        if (!m_cashiers[i]->IsBusy())
        {
            m_cashiers[i]->StartService(customer, currentTime);
            double serviceTime = m_serviceRandom->GetValue();
            ScheduleServiceEnd(i, serviceTime);
            assigned = true;
            break;
        }
    }
   
    if (!assigned)
    {
        m_queue.push(customer);
    }
   
    if (!m_stopped)
    {
        ScheduleNextArrival();
    }
}


void SupermarketSimulation::CustomerServiceEnd(uint32_t cashierId)
{
    if (m_stopped)
    {
        return;
    }
   
    double currentTime = Simulator::Now().GetSeconds();
    Ptr<Cashier> cashier = m_cashiers[cashierId];

    Ptr<Customer> customer = cashier->EndService(currentTime);
   
    if (customer == nullptr)
    {
        NS_LOG_ERROR("CustomerServiceEnd: Got null customer from cashier " << cashierId);
        return;
    }
   
    m_completedCustomers.push_back(customer);
   
    waitingTimes[m_numCashiers].push_back(customer->GetWaitingTime());
   
    if (!m_queue.empty())
    {
        Ptr<Customer> nextCustomer = m_queue.front();
        m_queue.pop();
        cashier->StartService(nextCustomer, currentTime);
        double serviceTime = m_serviceRandom->GetValue();
        ScheduleServiceEnd(cashierId, serviceTime);
    }
}


void SupermarketSimulation::ScheduleNextArrival()
{
    if (m_stopped)
    {
        return;
    }
   
    double currentTime = Simulator::Now().GetSeconds();
    double interArrivalTime = m_arrivalRandom->GetValue();
    double nextArrivalTime = currentTime + interArrivalTime;
   
    if (nextArrivalTime < m_simulationTime)
    {
        m_nextArrivalEvent = Simulator::Schedule(Seconds(interArrivalTime),
            &SupermarketSimulation::CustomerArrival, this);
    }
}


void SupermarketSimulation::ScheduleServiceEnd(uint32_t cashierId, double serviceTime)
{
    m_serviceEndEvents[cashierId] = Simulator::Schedule(Seconds(serviceTime),
        &SupermarketSimulation::CustomerServiceEnd, this, cashierId);
}


void SupermarketSimulation::PrintResults()
{
    double totalWaitingTime = 0;
    double totalServiceTime = 0;
    double totalIdleTime = 0;
   
    for (auto& wt : waitingTimes[m_numCashiers])
    {
        totalWaitingTime += wt;
    }
   
    for (auto& cashier : m_cashiers)
    {
        totalServiceTime += cashier->GetTotalServiceTime();
        totalIdleTime += cashier->GetTotalIdleTime();
    }
   
    double avgWaitingTime = (waitingTimes[m_numCashiers].size() > 0) ?
        totalWaitingTime / waitingTimes[m_numCashiers].size() : 0;
    double utilization = (totalServiceTime + totalIdleTime > 0) ?
        totalServiceTime / (totalServiceTime + totalIdleTime) : 0;
   
    double efficiencyScore = utilization / (avgWaitingTime + 1.0);
   
    CashierResults results;
    results.numCashiers = m_numCashiers;
    results.totalCustomers = m_completedCustomers.size();
    results.avgWaitingTime = avgWaitingTime;
    results.utilization = utilization;
    results.efficiencyScore = efficiencyScore;
    allResults.push_back(results);
   
    std::cout << "\nResults for " << m_numCashiers << " cashiers" << std::endl;
    std::cout << "Total customers served: " << m_completedCustomers.size() << std::endl;
    std::cout << "Average waiting time: " << std::fixed << std::setprecision(2) << avgWaitingTime << " seconds" << std::endl;
    std::cout << "System utilization: " << std::fixed << std::setprecision(1) << utilization * 100 << "%" << std::endl;
    std::cout << "Efficiency score: " << std::fixed << std::setprecision(3) << efficiencyScore << std::endl;
}


uint32_t FindOptimalCashiers(double minUtilization = 0.60, double maxUtilization = 0.90)
{
    if (allResults.empty())
    {
        return 0;
    }
   
    std::vector<CashierResults> goodResults;
    for (auto& result : allResults)
    {
        if (result.utilization >= minUtilization && result.utilization <= maxUtilization)
        {
            goodResults.push_back(result);
        }
    }
   
    if (goodResults.empty())
    {
        uint32_t bestCashiers = allResults[0].numCashiers;
        double bestScore = allResults[0].efficiencyScore;
       
        for (auto& result : allResults)
        {
            if (result.efficiencyScore > bestScore)
            {
                bestScore = result.efficiencyScore;
                bestCashiers = result.numCashiers;
            }
        }
        return bestCashiers;
    }
   
    uint32_t bestCashiers = goodResults[0].numCashiers;
    double bestWaitingTime = goodResults[0].avgWaitingTime;
   
    for (auto& result : goodResults)
    {
        if (result.avgWaitingTime < bestWaitingTime)
        {
            bestWaitingTime = result.avgWaitingTime;
            bestCashiers = result.numCashiers;
        }
    }
   
    return bestCashiers;
}


void GenerateUtilizationPlot(const std::string& filename = "utilization.plt")
{
    if (allResults.empty())
    {
        std::cerr << "No results available for plotting utilization." << std::endl;
        return;
    }
   
    std::vector<CashierResults> sortedResults = allResults;
    std::sort(sortedResults.begin(), sortedResults.end(),
        [](const CashierResults& a, const CashierResults& b) {
            return a.numCashiers < b.numCashiers;
        });
   
    std::string dataFile = "utilization_data.dat";
    std::ofstream dataOut(dataFile);
    if (!dataOut.is_open())
    {
        std::cerr << "Error: Could not open data file " << dataFile << " for writing." << std::endl;
        return;
    }
   
    dataOut << "# Cashiers Utilization(%)\n";
    for (const auto& result : sortedResults)
    {
        dataOut << result.numCashiers << " "
                << std::fixed << std::setprecision(2)
                << result.utilization * 100.0 << "\n";
    }
    dataOut.close();
   
    std::ofstream pltOut(filename);
    if (!pltOut.is_open())
    {
        std::cerr << "Error: Could not open PLT file " << filename << " for writing." << std::endl;
        return;
    }
   
    pltOut << "# GNUplot script for Cashier Utilization\n";
    pltOut << "# Generated by Supermarket Simulation\n\n";
    pltOut << "set terminal pngcairo enhanced color font 'Arial,12' size 800,600\n";
    pltOut << "set output 'utilization.png'\n\n";
    pltOut << "# To customize title font size, use: set title 'Title' font 'Arial,16'\n";
    pltOut << "set title 'Cashier Utilization vs Number of Cashiers'\n";
    pltOut << "set xlabel 'Number of Cashiers'\n";
    pltOut << "set ylabel 'Utilization (%)'\n";
    pltOut << "set grid linestyle 1 linecolor rgb '#cccccc'\n";
    pltOut << "set key top right\n";
    pltOut << "set xrange [0.5:*]\n";
    pltOut << "set yrange [0:105]\n";
    pltOut << "set xtics 1\n";
    pltOut << "set ytics 10\n";
    pltOut << "set style line 1 linecolor rgb '#0066cc' linewidth 2 pointtype 7 pointsize 1.5\n\n";
    pltOut << "plot '" << dataFile << "' using 1:2 with linespoints ls 1 title 'Utilization'\n";
    pltOut << "\n# To generate the plot, run: gnuplot " << filename << "\n";
   
    pltOut.close();
    std::cout << "Generated utilization plot script: " << filename << std::endl;
    std::cout << "  Data file: " << dataFile << std::endl;
    std::cout << "  Run: gnuplot " << filename << " to generate utilization.png" << std::endl;
}


void GenerateWaitingTimePlot(const std::string& filename = "waiting_time.plt")
{
    if (allResults.empty())
    {
        std::cerr << "No results available for plotting waiting time." << std::endl;
        return;
    }
   
    std::vector<CashierResults> sortedResults = allResults;
    std::sort(sortedResults.begin(), sortedResults.end(),
        [](const CashierResults& a, const CashierResults& b) {
            return a.numCashiers < b.numCashiers;
        });
   
    std::string dataFile = "waiting_time_data.dat";
    std::ofstream dataOut(dataFile);
    if (!dataOut.is_open())
    {
        std::cerr << "Error: Could not open data file " << dataFile << " for writing." << std::endl;
        return;
    }
   
    dataOut << "# Cashiers AvgWaitingTime(seconds)\n";
    for (const auto& result : sortedResults)
    {
        dataOut << result.numCashiers << " "
                << std::fixed << std::setprecision(3)
                << result.avgWaitingTime << "\n";
    }
    dataOut.close();
   
    std::ofstream pltOut(filename);
    if (!pltOut.is_open())
    {
        std::cerr << "Error: Could not open PLT file " << filename << " for writing." << std::endl;
        return;
    }
   
    pltOut << "# GNUplot script for Average Waiting Time\n";
    pltOut << "# Generated by Supermarket Simulation\n\n";
    pltOut << "set terminal pngcairo enhanced color font 'Arial,12' size 800,600\n";
    pltOut << "set output 'waiting_time.png'\n\n";
    pltOut << "# To customize title font size, use: set title 'Title' font 'Arial,16'\n";
    pltOut << "set title 'Average Waiting Time vs Number of Cashiers'\n";
    pltOut << "set xlabel 'Number of Cashiers'\n";
    pltOut << "set ylabel 'Average Waiting Time (seconds)'\n";
    pltOut << "set grid linestyle 1 linecolor rgb '#cccccc'\n";
    pltOut << "set key top right\n";
    pltOut << "set xrange [0.5:*]\n";
    pltOut << "set yrange [0:*]\n";
    pltOut << "set xtics 1\n";
    pltOut << "# Uncomment the next line for logarithmic y-axis (useful for large variations)\n";
    pltOut << "# set logscale y\n";
    pltOut << "set style line 1 linecolor rgb '#cc0000' linewidth 2 pointtype 7 pointsize 1.5\n\n";
    pltOut << "plot '" << dataFile << "' using 1:2 with linespoints ls 1 title 'Average Waiting Time'\n";
    pltOut << "\n# To generate the plot, run: gnuplot " << filename << "\n";
   
    pltOut.close();
    std::cout << "Generated waiting time plot script: " << filename << std::endl;
    std::cout << "  Data file: " << dataFile << std::endl;
    std::cout << "  Run: gnuplot " << filename << " to generate waiting_time.png" << std::endl;
}


int main(int argc, char *argv[])
{
    // Set up command line parameters
    uint32_t maxCashiers = 10;
    double arrivalRate = 2.0;  // customers per second
    double serviceRate = 1.0;  // customers per second
    double simulationTime = 1000.0;  // seconds
   
    CommandLine cmd;
    cmd.AddValue("maxCashiers", "Maximum number of cashiers to test", maxCashiers);
    cmd.AddValue("arrivalRate", "Customer arrival rate (customers/second)", arrivalRate);
    cmd.AddValue("serviceRate", "Service rate per cashier (customers/second)", serviceRate);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.Parse(argc, argv);
   
    LogComponentEnable("SupermarketSimulation", LOG_LEVEL_INFO);
   
    allResults.clear();
    waitingTimes.clear();
   
    uint32_t expectedCustomers = static_cast<uint32_t>(arrivalRate * simulationTime);
   
    std::cout << "Supermarket M/M/c Queue Simulation" << std::endl;
    std::cout << "Arrival rate: " << arrivalRate << " customers/second" << std::endl;
    std::cout << "Service rate: " << serviceRate << " customers/second" << std::endl;
    std::cout << "Simulation time: " << simulationTime << " seconds" << std::endl;
    std::cout << "Expected customers: ~" << expectedCustomers << std::endl;
    std::cout << "Testing 1 to " << maxCashiers << " cashiers" << std::endl;
   
    for (uint32_t numCashiers = 1; numCashiers <= maxCashiers; numCashiers++)
    {
        SupermarketSimulation sim(numCashiers, arrivalRate, serviceRate);
       
        sim.RunSimulation(simulationTime);
       
        sim.PrintResults();
       
        Simulator::Destroy();
    }
   
    std::cout << "\n Comparison Table " << std::endl;
    std::cout << "Cashiers | Customers | Avg Wait Time | Utilization | Efficiency" << std::endl;
    std::cout << "---------|-----------|---------------|-------------|------------" << std::endl;
   
    for (auto& result : allResults)
    {
        std::cout << std::setw(8) << result.numCashiers << " | "
                  << std::setw(9) << result.totalCustomers << " | "
                  << std::setw(13) << std::fixed << std::setprecision(2) << result.avgWaitingTime << " | "
                  << std::setw(11) << std::fixed << std::setprecision(1) << result.utilization * 100 << "% | "
                  << std::setw(10) << std::fixed << std::setprecision(3) << result.efficiencyScore << std::endl;
    }
   
    uint32_t optimalCashiers = FindOptimalCashiers();
   
    if (optimalCashiers > 0)
    {
        CashierResults* optimalResult = nullptr;
        for (auto& result : allResults)
        {
            if (result.numCashiers == optimalCashiers)
            {
                optimalResult = &result;
                break;
            }
        }
       
        if (optimalResult != nullptr)
        {
            std::cout << "\nRECOMMENDATION" << std::endl;
            std::cout << "Optimal number of cashiers: " << optimalCashiers << std::endl;
            std::cout << "For " << expectedCustomers << " expected customers:" << std::endl;
            std::cout << "  - Average waiting time: " << std::fixed << std::setprecision(2)
                      << optimalResult->avgWaitingTime << " seconds" << std::endl;
            std::cout << "  - System utilization: " << std::fixed << std::setprecision(1)
                      << optimalResult->utilization * 100 << "%" << std::endl;
            std::cout << "  - Efficiency score: " << std::fixed << std::setprecision(3)
                      << optimalResult->efficiencyScore << std::endl;
            std::cout << "\nThis configuration balances low waiting time with high utilization." << std::endl;
        }
    }
   
    GenerateUtilizationPlot();
    GenerateWaitingTimePlot();
    std::cout << "\nPlot files generated successfully!" << std::endl;
   
    return 0;
}










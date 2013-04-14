
lang = naev.lang()
if lang == 'es' then --not translated atm
else --default english

   events = {

      {
         "famine",  --event name/type
         720000,         --event takes 12 min (time is in ms)
         {"Food", 2.0} --commodity and it's new relative (preffered) price
      },

      {
         "bumper harvest",
         720000,
         {"Food", .5} --preferred food price now at half the price it was

      },

      {
         "worker's strike",
         720000,
         {"Industrial Goods", 1.5},
         {"Ore", 1.5}
      },

      {
         "cat convention",
         720000,
         {"Luxury Goods", 1.5},
         {"Medicine",1.25}
      },

      {
         "disease outbreak",
         720000,
         {"Medicine",2},
         {"Food",1.3}
      }
   }

end

commodities={"Food", "Medicine", "Luxury Goods","Industrial Goods", "Ore"}
factions = {"Empire","Sirius","Frontier","Soromid","Dvaered","Independent"}

weighted = 0     --if sys was originally weighted
original_preferred={}   --original preferred and real prices
original_real={}
new_preferred={}



function create()

   -- do return end--tmp, to figure out other issues

   make_event()

end


   --make the news event for the selected event and system
function make_article(sys, event)

   title = "System "..sys:name().."  experienced "..event[1]
   body = "System "..sys:name().." recently experienced a "..event[1]..", changing "

      --say how the prices have changed
   for i=1, #event-2 do
      comm_name = event[i+2][1]
      body = body.."the price of "..comm_name..string.format(" from %.0f credits per ton to %.0f",original_real[i],economy.getPrice(sys, comm_name))
      if i<#event-3 then
         body = body..", "
      elseif i==#event-3 then
         body = body..", and "
      elseif i==#event-2 then
         body = body.."."
      end
   end
      --make the article
   article = news.add("Generic", title, body, time.get() + time.create( 0, event[2]/36000, 0)) --it'll dissapear in event length (in ms) / 36000 STP
   article:bind("economic event")

end


   --make a single economic event
function make_event()

      --get the event
   event_num = math.random(#events)
   event = events[event_num]

   economic_articles = news.get("economic events")  --get all events, we don't want to do some place twice

      --get the system, a populated one with at least 500 total presence
   while 1 do
      sys=system.get(true)
      syspresences = system.presences(sys)
      sum=0
      for _,v in ipairs(factions) do
         if syspresences[v] then
            sum=sum+syspresences[v]
         end
      end
      system_nottaken=true
      for i=1,#economic_articles do --check this system wasn't already taken
         if string.match( article:title(), sys:name()) then
            system_nottaken=false
            break
         end
      end
      if system_nottaken and sum > 500 then
         break
      end
   end

   --rm me
   print("At "..sys:name().." doing event "..event[1])

      --get the original prices
   for i=1,#event-2 do
      comm_name = event[i+2][1]
      original_preferred[i] = economy.getPreferredPrice(sys, comm_name)
      original_real[i] = economy.getPrice(sys, comm_name)
   end

      --if no preferred prices, put the preferred prices to current reals as defaults, and set weight to 1
   if economy.getSysWeight(sys)==0 then
      economy.setSysWeight(sys,1.0)
      for i=1, #commodities do
         economy.setPreferredPrice( sys, commodities[i], economy.getPrice(sys, commodities[i])/commodity.get(commodities[i]):getprice() )
      end
   else
      weighted=1
   end

      --put in new prices
   for i=1,#event-2 do
      comm = event[i+2]
      price = economy.getPreferredPrice(sys, comm[1])
      if price==0 then
         price = economy.getPrice(sys, comm[1]) / commodity.get(comm[1]):getprice()
      end
      price = price*comm[2] 
      economy.setPreferredPrice(sys, comm[1], price)
      new_preferred[i] = price
   end

      --update the prices, and make the article
   economy.updateSolutions()
   economy.updatePrices()

   make_article(sys, event)

      --set up the event ending
      --put all the information we'll need into a string
   str = ""
   str=" system:"..sys:name()..str..", weighted:"..weighted
   for i=1,#event-2 do
      comm_name = event[i+2][1]
      str=str..string.format(" %sorigprefprice:%f %snewprefprice:%f",comm_name, original_preferred[i], comm_name, new_preferred[i] )
   end
   hook.timer(event[2], "end_event", str )

end


--end the event, and return the values to their original values
function end_event(str)

      --first, get all the information we'll need
   sys=nil
   num_changed_comms=0
   weighted=0
   comms={}

   tmp    = str:match("system:[a-zA-Z' ]*,")
   sys    = system.get(tmp:match(":[a-zA-Z' ]*"):sub(2))
   tmp    = str:match("weighted:[0-9]*")
   weight = tmp:match("[0-9]*")
   for i=1,#commodities do
      tmp = str:match(commodities[i].."origprefprice:[0-9]*")
      if (tmp) then
         num_changed_comms=num_changed_comms+1
         tmp2 = str:match(commodities[i].."newprefprice:[0-9]*")

         tmp  = tmp:match("[0-9]*")
         tmp2 = tmp2:match("[0-9]*")
         comms[num_changed_comms] = {tmp, tmp2}
      end
   end


   print("ending event at"..sys:name())

   if weighted==0 then --if system was unweighted, just unweight it
      economy.setSysWeight(sys, 0.0)
      economy.updateSolutions()
      economy.updatePrices()
      return
   end

      --add the difference in original and event prices, and add it, to get back to original price 
   for i=1, #orig_pref do
      diff = orig_pref-new_pref
      comm_name = event[i+2][2]
      new_val = economy.getPreferredPrice(sys, comm_name) + diff
      economy.setPreferredPrice(sys, comm_name, new_val)
   end

end

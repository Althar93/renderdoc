/******************************************************************************
 * The MIT License (MIT)
 * 
 * Copyright (c) 2015-2016 Baldur Karlsson
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "../vk_core.h"

string WrappedVulkan::MakeRenderPassOpString(bool store)
{
	string opDesc = "";

	const VulkanCreationInfo::RenderPass &info = m_CreationInfo.m_RenderPass[m_BakedCmdBufferInfo[m_LastCmdBufferID].state.renderPass];
	const VulkanCreationInfo::Framebuffer &fbinfo = m_CreationInfo.m_Framebuffer[m_BakedCmdBufferInfo[m_LastCmdBufferID].state.framebuffer];

	const vector<VulkanCreationInfo::RenderPass::Attachment> &atts = info.attachments;

	if(atts.empty())
	{
		opDesc = "-";
	}
	else
	{
		bool colsame = true;

		// find which attachment is the depth-stencil one
		int32_t dsAttach = info.subpasses[m_BakedCmdBufferInfo[m_LastCmdBufferID].state.subpass].depthstencilAttachment;
		bool hasStencil = false;
		bool depthonly = false;

		// if there is a depth-stencil attachment, see if it has a stencil
		// component and if the subpass is depth only (no other attachments)
		if(dsAttach >= 0)
		{
			hasStencil = !IsDepthOnlyFormat(fbinfo.attachments[dsAttach].format);
			depthonly = info.subpasses[m_BakedCmdBufferInfo[m_LastCmdBufferID].state.subpass].colorAttachments.size() == 0;
		}

		// first colour attachment, if there is one
		int32_t colAttach = 0;
		if(!depthonly && dsAttach == 0)
			colAttach = 1;

		// look through all other non-depth attachments to see if they're
		// identical
		for(size_t i=0; i < atts.size(); i++)
		{
			if((int32_t)i == dsAttach)
				continue;

			if((int32_t)i == colAttach)
				continue;

			if(store)
			{
				if(atts[i].storeOp != atts[colAttach].storeOp)
					colsame = false;
			}
			else
			{
				if(atts[i].loadOp != atts[colAttach].loadOp)
					colsame = false;
			}
		}

		// handle depth only passes
		if(depthonly)
		{
			opDesc = "";
		}
		else if(!colsame)
		{
			// if we have different storage for the colour, don't display
			// the full details

			opDesc = store ? "Different store ops" : "Different load ops";
		}
		else
		{
			// all colour ops are the same, print it
			opDesc = store ? ToStr::Get(atts[colAttach].storeOp) : ToStr::Get(atts[colAttach].loadOp);
		}
		
		// do we have depth?
		if(dsAttach != -1)
		{
			// could be empty if this is a depth-only pass
			if(!opDesc.empty())
				opDesc = "C=" + opDesc + ", ";

			// if there's no stencil, just print depth op
			if(!hasStencil)
			{
				opDesc += "D=" + (store ? ToStr::Get(atts[dsAttach].storeOp) : ToStr::Get(atts[dsAttach].loadOp));
			}
			else
			{
				if(store)
				{
					// if depth and stencil have same op, print together, otherwise separately
					if(atts[dsAttach].storeOp == atts[dsAttach].stencilStoreOp)
						opDesc += "DS=" + ToStr::Get(atts[dsAttach].storeOp);
					else
						opDesc += "D=" + ToStr::Get(atts[dsAttach].storeOp) + ", S=" + ToStr::Get(atts[dsAttach].stencilStoreOp);
				}
				else
				{
					// if depth and stencil have same op, print together, otherwise separately
					if(atts[dsAttach].loadOp == atts[dsAttach].stencilLoadOp)
						opDesc += "DS=" + ToStr::Get(atts[dsAttach].loadOp);
					else
						opDesc += "D=" + ToStr::Get(atts[dsAttach].loadOp) + ", S=" + ToStr::Get(atts[dsAttach].stencilLoadOp);
				}
			}
		}
	}

	return opDesc;
}

// Command pool functions

bool WrappedVulkan::Serialise_vkCreateCommandPool(
			Serialiser*                                 localSerialiser,
			VkDevice                                    device,
			const VkCommandPoolCreateInfo*              pCreateInfo,
			const VkAllocationCallbacks*                pAllocator,
			VkCommandPool*                              pCmdPool)
{
	SERIALISE_ELEMENT(ResourceId, devId, GetResID(device));
	SERIALISE_ELEMENT(VkCommandPoolCreateInfo, info, *pCreateInfo);
	SERIALISE_ELEMENT(ResourceId, id, GetResID(*pCmdPool));

	if(m_State == READING)
	{
		device = GetResourceManager()->GetLiveHandle<VkDevice>(devId);
		VkCommandPool pool = VK_NULL_HANDLE;

		VkResult ret = ObjDisp(device)->CreateCommandPool(Unwrap(device), &info, NULL, &pool);

		if(ret != VK_SUCCESS)
		{
			RDCERR("Failed on resource serialise-creation, VkResult: 0x%08x", ret);
		}
		else
		{
			ResourceId live = GetResourceManager()->WrapResource(Unwrap(device), pool);
			GetResourceManager()->AddLiveResource(id, pool);
		}
	}

	return true;
}

VkResult WrappedVulkan::vkCreateCommandPool(
			VkDevice                                    device,
			const VkCommandPoolCreateInfo*              pCreateInfo,
			const VkAllocationCallbacks*                pAllocator,
			VkCommandPool*                              pCmdPool)
{
	VkResult ret = ObjDisp(device)->CreateCommandPool(Unwrap(device), pCreateInfo, pAllocator, pCmdPool);

	if(ret == VK_SUCCESS)
	{
		ResourceId id = GetResourceManager()->WrapResource(Unwrap(device), *pCmdPool);
		
		if(m_State >= WRITING)
		{
			Chunk *chunk = NULL;
			
			{
				CACHE_THREAD_SERIALISER();

				SCOPED_SERIALISE_CONTEXT(CREATE_CMD_POOL);
				Serialise_vkCreateCommandPool(localSerialiser, device, pCreateInfo, NULL, pCmdPool);

				chunk = scope.Get();
			}

			VkResourceRecord *record = GetResourceManager()->AddResourceRecord(*pCmdPool);
			record->AddChunk(chunk);
		}
		else
		{
			GetResourceManager()->AddLiveResource(id, *pCmdPool);
		}
	}

	return ret;
}

VkResult WrappedVulkan::vkResetCommandPool(
			VkDevice                                    device,
			VkCommandPool                               cmdPool,
			VkCommandPoolResetFlags                     flags)
{
	return ObjDisp(device)->ResetCommandPool(Unwrap(device), Unwrap(cmdPool), flags);
}


// Command buffer functions

VkResult WrappedVulkan::vkAllocateCommandBuffers(
	VkDevice                                    device,
	const VkCommandBufferAllocateInfo*          pAllocateInfo,
	VkCommandBuffer*                            pCommandBuffers)
{
	VkCommandBufferAllocateInfo unwrappedInfo = *pAllocateInfo;
	unwrappedInfo.commandPool = Unwrap(unwrappedInfo.commandPool);
	VkResult ret = ObjDisp(device)->AllocateCommandBuffers(Unwrap(device), &unwrappedInfo, pCommandBuffers);

	if(ret == VK_SUCCESS)
	{
		for(uint32_t i=0; i < unwrappedInfo.commandBufferCount; i++)
		{
			SetDispatchTableOverMagicNumber(device, pCommandBuffers[i]);

			ResourceId id = GetResourceManager()->WrapResource(Unwrap(device), pCommandBuffers[i]);

			// loader expects command buffers to have the magic number in them
			SetMagicNumberOverDispatchTable(pCommandBuffers[i]);

			if(m_State >= WRITING)
			{
				VkResourceRecord *record = GetResourceManager()->AddResourceRecord(pCommandBuffers[i]);

				record->bakedCommands = NULL;

				record->pool = GetRecord(pAllocateInfo->commandPool);
				record->AddParent(record->pool);

				{
					record->pool->LockChunks();
					record->pool->pooledChildren.push_back(record);
					record->pool->UnlockChunks();
				}

				// we don't serialise this as we never create this command buffer directly.
				// Instead we create a command buffer for each baked list that we find.

				// if pNext is non-NULL, need to do a deep copy
				// we don't support any extensions on VkCommandBufferCreateInfo anyway
				RDCASSERT(pAllocateInfo->pNext == NULL);

				record->cmdInfo = new CmdBufferRecordingInfo();

				record->cmdInfo->device = device;
				record->cmdInfo->allocInfo = *pAllocateInfo;
				record->cmdInfo->allocInfo.commandBufferCount = 1;
			}
			else
			{
				GetResourceManager()->AddLiveResource(id, pCommandBuffers[i]);
			}
		}
	}

	return ret;
}

bool WrappedVulkan::Serialise_vkBeginCommandBuffer(
			Serialiser*                                 localSerialiser,
			VkCommandBuffer                                 commandBuffer,
			const VkCommandBufferBeginInfo*                 pBeginInfo)
{
	SERIALISE_ELEMENT(ResourceId, cmdId, GetResID(commandBuffer));

	ResourceId bakedCmdId;
	VkCommandBufferAllocateInfo allocInfo;
	VkDevice device = VK_NULL_HANDLE;

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetResourceManager()->GetResourceRecord(cmdId);
		RDCASSERT(record->bakedCommands);
		if(record->bakedCommands)
			bakedCmdId = record->bakedCommands->GetResourceID();
		
		RDCASSERT(record->cmdInfo);
		device = record->cmdInfo->device;
		allocInfo = record->cmdInfo->allocInfo;
	}

	SERIALISE_ELEMENT(VkCommandBufferBeginInfo, info, *pBeginInfo);
	SERIALISE_ELEMENT(ResourceId, bakeId, bakedCmdId);

	if(m_State < WRITING)
	{
		m_LastCmdBufferID = cmdId;
		m_CmdBuffersInProgress++;
	}
	
	SERIALISE_ELEMENT(ResourceId, devId, GetResID(device));
	localSerialiser->Serialise("allocInfo", allocInfo);

	if(m_State < WRITING)
		device = GetResourceManager()->GetLiveHandle<VkDevice>(devId);

	if(m_State == EXECUTING)
	{
		const vector<uint32_t> &baseEvents = m_PartialReplayData.cmdBufferSubmits[bakeId];
		uint32_t length = m_BakedCmdBufferInfo[bakeId].eventCount;

		bool partial = false;

		for(auto it=baseEvents.begin(); it != baseEvents.end(); ++it)
		{
			if(*it <= m_LastEventID && m_LastEventID < (*it + length))
			{
				RDCDEBUG("vkBegin - partial detected %u < %u < %u, %llu -> %llu", *it, m_LastEventID, *it + length, cmdId, bakeId);

				m_PartialReplayData.partialParent = cmdId;
				m_PartialReplayData.baseEvent = *it;
				m_PartialReplayData.renderPassActive = false;
				m_PartialReplayData.partialDevice = device;
				m_PartialReplayData.resultPartialCmdPool = (VkCommandPool)(uint64_t)GetResourceManager()->GetNonDispWrapper(allocInfo.commandPool);
				
				partial = true;
			}
		}
		
		if(partial || (m_DrawcallCallback && m_DrawcallCallback->RecordAllCmds()))
		{
			// pull all re-recorded commands from our own device and command pool for easier cleanup
			if(!partial)
			{
				device = GetDev();
				allocInfo.commandPool = Unwrap(m_InternalCmds.cmdpool);
			}

			VkCommandBuffer cmd = VK_NULL_HANDLE;
			VkResult ret = ObjDisp(device)->AllocateCommandBuffers(Unwrap(device), &allocInfo, &cmd);

			if(ret != VK_SUCCESS)
			{
				RDCERR("Failed on resource serialise-creation, VkResult: 0x%08x", ret);
			}
			else
			{
				GetResourceManager()->WrapResource(Unwrap(device), cmd);
			}

			if(partial)
			{
				m_PartialReplayData.resultPartialCmdBuffer = cmd;
			}
			else
			{
				// we store under both baked and non baked ID.
				// The baked ID is the 'real' entry, the non baked is simply so it
				// can be found in the subsequent serialised commands that ref the
				// non-baked ID. The baked ID is referenced by the submit itself.
				//
				// In vkEndCommandBuffer we erase the non-baked reference, and since
				// we know you can only be recording a command buffer once at a time
				// (even if it's baked to several command buffers in the frame)
				// there's no issue with clashes here.
				m_RerecordCmds[bakeId] = cmd;
				m_RerecordCmds[cmdId] = cmd;
			}

			// add one-time submit flag as this partial cmd buffer will only be submitted once
			info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

			ObjDisp(cmd)->BeginCommandBuffer(Unwrap(cmd), &info);
		}

		m_BakedCmdBufferInfo[cmdId].curEventID = 0;
	}
	else if(m_State == READING)
	{
		// remove one-time submit flag as we will want to submit many
		info.flags &= ~VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VkCommandBuffer cmd = VK_NULL_HANDLE;

		if(!GetResourceManager()->HasLiveResource(bakeId))
		{
			VkResult ret = ObjDisp(device)->AllocateCommandBuffers(Unwrap(device), &allocInfo, &cmd);

			if(ret != VK_SUCCESS)
			{
				RDCERR("Failed on resource serialise-creation, VkResult: 0x%08x", ret);
			}
			else
			{
				ResourceId live = GetResourceManager()->WrapResource(Unwrap(device), cmd);
				GetResourceManager()->AddLiveResource(bakeId, cmd);
			}

			// whenever a vkCmd command-building chunk asks for the command buffer, it
			// will get our baked version.
			GetResourceManager()->ReplaceResource(cmdId, bakeId);
		}
		else
		{
			cmd = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(bakeId);
		}

		{
			VulkanDrawcallTreeNode *draw = new VulkanDrawcallTreeNode;
			m_BakedCmdBufferInfo[cmdId].draw = draw;
			
			// On queue submit we increment all child events/drawcalls by
			// m_RootEventID and insert them into the tree.
			m_BakedCmdBufferInfo[cmdId].curEventID = 0;
			m_BakedCmdBufferInfo[cmdId].eventCount = 0;
			m_BakedCmdBufferInfo[cmdId].drawCount = 0;

			m_BakedCmdBufferInfo[cmdId].drawStack.push_back(draw);
		}

		ObjDisp(device)->BeginCommandBuffer(Unwrap(cmd), &info);
	}

	return true;
}

VkResult WrappedVulkan::vkBeginCommandBuffer(
			VkCommandBuffer                                 commandBuffer,
			const VkCommandBufferBeginInfo*                 pBeginInfo)
{
	VkResourceRecord *record = GetRecord(commandBuffer);
	RDCASSERT(record);

	if(record)
	{
		// If a command bfufer was already recorded (ie we have some baked commands),
		// then begin is spec'd to implicitly reset. That means we need to tidy up
		// any existing baked commands before creating a new set.
		if(record->bakedCommands)
			record->bakedCommands->Delete(GetResourceManager());

		record->bakedCommands = GetResourceManager()->AddResourceRecord(ResourceIDGen::GetNewUniqueID());
		record->bakedCommands->SpecialResource = true;
		record->bakedCommands->Resource = (WrappedVkRes *)commandBuffer;
		record->bakedCommands->cmdInfo = new CmdBufferRecordingInfo();

		record->bakedCommands->cmdInfo->device = record->cmdInfo->device;
		record->bakedCommands->cmdInfo->allocInfo = record->cmdInfo->allocInfo;

		{
			CACHE_THREAD_SERIALISER();

			SCOPED_SERIALISE_CONTEXT(BEGIN_CMD_BUFFER);
			Serialise_vkBeginCommandBuffer(localSerialiser, commandBuffer, pBeginInfo);
			
			record->AddChunk(scope.Get());
		}
	}

	VkCommandBufferInheritanceInfo unwrappedInfo;
	if(pBeginInfo->pInheritanceInfo)
	{
		unwrappedInfo = *pBeginInfo->pInheritanceInfo;
		unwrappedInfo.framebuffer = Unwrap(unwrappedInfo.framebuffer);
		unwrappedInfo.renderPass = Unwrap(unwrappedInfo.renderPass);

		VkCommandBufferBeginInfo beginInfo = *pBeginInfo;
		beginInfo.pInheritanceInfo = &unwrappedInfo;
		return ObjDisp(commandBuffer)->BeginCommandBuffer(Unwrap(commandBuffer), &beginInfo);
	}

	return ObjDisp(commandBuffer)->BeginCommandBuffer(Unwrap(commandBuffer), pBeginInfo);
}

bool WrappedVulkan::Serialise_vkEndCommandBuffer(Serialiser* localSerialiser, VkCommandBuffer commandBuffer)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));

	ResourceId bakedCmdId;

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetResourceManager()->GetResourceRecord(cmdid);
		RDCASSERT(record->bakedCommands);
		if(record->bakedCommands)
			bakedCmdId = record->bakedCommands->GetResourceID();
	}

	SERIALISE_ELEMENT(ResourceId, bakeId, bakedCmdId);

	if(m_State < WRITING)
	{
		m_LastCmdBufferID = cmdid;
		m_CmdBuffersInProgress--;
	}
	
	if(m_State == EXECUTING)
	{
		if(ShouldRerecordCmd(cmdid))
		{
			commandBuffer = RerecordCmdBuf(cmdid);
			RDCDEBUG("Ending partial command buffer for %llu baked to %llu", cmdid, bakeId);

			bool recordAll = m_DrawcallCallback && m_DrawcallCallback->RecordAllCmds();

			if(!recordAll && m_PartialReplayData.renderPassActive)
			{
				uint32_t numSubpasses = (uint32_t)m_CreationInfo.m_RenderPass[m_RenderState.renderPass].subpasses.size();

				for(uint32_t sub=m_RenderState.subpass; sub < numSubpasses-1; sub++)
					ObjDisp(commandBuffer)->CmdNextSubpass(Unwrap(commandBuffer), VK_SUBPASS_CONTENTS_INLINE);

				ObjDisp(commandBuffer)->CmdEndRenderPass(Unwrap(commandBuffer));
			}

			ObjDisp(commandBuffer)->EndCommandBuffer(Unwrap(commandBuffer));

			// erase the non-baked reference to this command buffer so that we don't have
			// duplicates when it comes time to clean up. See above in vkBeginCommandBuffer
			m_RerecordCmds.erase(cmdid);

			m_PartialReplayData.partialParent = ResourceId();
		}

		m_BakedCmdBufferInfo[cmdid].curEventID = 0;
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(bakeId);

		GetResourceManager()->RemoveReplacement(cmdid);

		ObjDisp(commandBuffer)->EndCommandBuffer(Unwrap(commandBuffer));
		
		if(m_State == READING && !m_BakedCmdBufferInfo[m_LastCmdBufferID].curEvents.empty())
		{
			FetchDrawcall draw;
			draw.name = "API Calls";
			draw.flags |= eDraw_SetMarker;

			AddDrawcall(draw, true);

			m_BakedCmdBufferInfo[m_LastCmdBufferID].curEventID++;
		}

		{
			if(GetDrawcallStack().size() > 1)
				GetDrawcallStack().pop_back();
		}

		{
			m_BakedCmdBufferInfo[bakeId].draw = m_BakedCmdBufferInfo[m_LastCmdBufferID].draw;
			m_BakedCmdBufferInfo[bakeId].curEvents = m_BakedCmdBufferInfo[m_LastCmdBufferID].curEvents;
			m_BakedCmdBufferInfo[bakeId].curEventID = 0;
			m_BakedCmdBufferInfo[bakeId].eventCount = m_BakedCmdBufferInfo[m_LastCmdBufferID].curEventID;
			m_BakedCmdBufferInfo[bakeId].drawCount = m_BakedCmdBufferInfo[m_LastCmdBufferID].drawCount;
			
			m_BakedCmdBufferInfo[m_LastCmdBufferID].draw = NULL;
			m_BakedCmdBufferInfo[m_LastCmdBufferID].curEventID = 0;
			m_BakedCmdBufferInfo[m_LastCmdBufferID].eventCount = 0;
			m_BakedCmdBufferInfo[m_LastCmdBufferID].drawCount = 0;
		}
	}

	return true;
}

VkResult WrappedVulkan::vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
	VkResourceRecord *record = GetRecord(commandBuffer);
	RDCASSERT(record);

	if(record)
	{
		// ensure that we have a matching begin
		RDCASSERT(record->bakedCommands);

		{
			CACHE_THREAD_SERIALISER();

			SCOPED_SERIALISE_CONTEXT(END_CMD_BUFFER);
			Serialise_vkEndCommandBuffer(localSerialiser, commandBuffer);
			
			record->AddChunk(scope.Get());
		}

		record->Bake();
	}

	return ObjDisp(commandBuffer)->EndCommandBuffer(Unwrap(commandBuffer));
}

VkResult WrappedVulkan::vkResetCommandBuffer(
	  VkCommandBuffer                                 commandBuffer,
    VkCommandBufferResetFlags                       flags)
{
	VkResourceRecord *record = GetRecord(commandBuffer);
	RDCASSERT(record);

	if(record)
	{
		// all we need to do is remove the existing baked commands.
		// The application will still need to call begin command buffer itself.
		// this function is essentially a driver hint as it cleans up implicitly
		// on begin.
		//
		// Because it's totally legal for an application to record, submit, reset,
		// record, submit again, and we need some way of referencing the two different
		// sets of commands on replay, our command buffers are given new unique IDs
		// each time they are begun, so on replay it looks like they were all unique
		// (albeit with the same properties for those that share a 'parent'). Hence,
		// we don't need to record or replay when a ResetCommandBuffer happens
		if(record->bakedCommands)
			record->bakedCommands->Delete(GetResourceManager());
		
		record->bakedCommands = NULL;
	}

	return ObjDisp(commandBuffer)->ResetCommandBuffer(Unwrap(commandBuffer), flags);
}

// Command buffer building functions

bool WrappedVulkan::Serialise_vkCmdBeginRenderPass(
			Serialiser*                                 localSerialiser,
			VkCommandBuffer                             commandBuffer,
			const VkRenderPassBeginInfo*                pRenderPassBegin,
			VkSubpassContents                           contents)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(VkRenderPassBeginInfo, beginInfo, *pRenderPassBegin);
	SERIALISE_ELEMENT(VkSubpassContents, cont, contents);

	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	if(m_State == EXECUTING)
	{
		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);

			m_PartialReplayData.renderPassActive = true;
			ObjDisp(commandBuffer)->CmdBeginRenderPass(Unwrap(commandBuffer), &beginInfo, cont);

			m_RenderState.subpass = 0;

			m_RenderState.renderPass = GetResourceManager()->GetNonDispWrapper(beginInfo.renderPass)->id;
			m_RenderState.framebuffer = GetResourceManager()->GetNonDispWrapper(beginInfo.framebuffer)->id;
			m_RenderState.renderArea = beginInfo.renderArea;
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);

		ObjDisp(commandBuffer)->CmdBeginRenderPass(Unwrap(commandBuffer), &beginInfo, cont);
		
		// track while reading, for fetching the right set of outputs in AddDrawcall
		m_BakedCmdBufferInfo[m_LastCmdBufferID].state.subpass = 0;
		m_BakedCmdBufferInfo[m_LastCmdBufferID].state.renderPass = GetResourceManager()->GetNonDispWrapper(beginInfo.renderPass)->id;
		m_BakedCmdBufferInfo[m_LastCmdBufferID].state.framebuffer = GetResourceManager()->GetNonDispWrapper(beginInfo.framebuffer)->id;

		const string desc = localSerialiser->GetDebugStr();
		
		string opDesc = MakeRenderPassOpString(false);

		AddEvent(BEGIN_RENDERPASS, desc);
		FetchDrawcall draw;
		draw.name = StringFormat::Fmt("vkCmdBeginRenderPass(%s)", opDesc.c_str());
		draw.flags |= eDraw_PassBoundary|eDraw_BeginPass;

		AddDrawcall(draw, true);
	}

	return true;
}

void WrappedVulkan::vkCmdBeginRenderPass(
			VkCommandBuffer                             commandBuffer,
			const VkRenderPassBeginInfo*                pRenderPassBegin,
			VkSubpassContents                           contents)
{
	VkRenderPassBeginInfo unwrappedInfo = *pRenderPassBegin;
	unwrappedInfo.renderPass = Unwrap(unwrappedInfo.renderPass);
	unwrappedInfo.framebuffer = Unwrap(unwrappedInfo.framebuffer);
	ObjDisp(commandBuffer)->CmdBeginRenderPass(Unwrap(commandBuffer), &unwrappedInfo, contents);

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(BEGIN_RENDERPASS);
		Serialise_vkCmdBeginRenderPass(localSerialiser, commandBuffer, pRenderPassBegin, contents);

		record->AddChunk(scope.Get());
		record->MarkResourceFrameReferenced(GetResID(pRenderPassBegin->renderPass), eFrameRef_Read);

		VkResourceRecord *fb = GetRecord(pRenderPassBegin->framebuffer);
		
		record->MarkResourceFrameReferenced(fb->GetResourceID(), eFrameRef_Read);
		for(size_t i=0; i < VkResourceRecord::MaxImageAttachments; i++)
		{
			if(fb->imageAttachments[i] == NULL) break;

			record->MarkResourceFrameReferenced(fb->imageAttachments[i]->baseResource, eFrameRef_Write);
			if(fb->imageAttachments[i]->baseResourceMem != ResourceId())
				record->MarkResourceFrameReferenced(fb->imageAttachments[i]->baseResourceMem, eFrameRef_Read);
			if(fb->imageAttachments[i]->sparseInfo)
				record->cmdInfo->sparse.insert(fb->imageAttachments[i]->sparseInfo);
			record->cmdInfo->dirtied.insert(fb->imageAttachments[i]->baseResource);
		}
	}
}

bool WrappedVulkan::Serialise_vkCmdNextSubpass(
	Serialiser*                                 localSerialiser,
	VkCommandBuffer                             commandBuffer,
	VkSubpassContents                           contents)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(VkSubpassContents, cont, contents);
	
	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	if(m_State == EXECUTING)
	{
		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);

			m_RenderState.subpass++;

			ObjDisp(commandBuffer)->CmdNextSubpass(Unwrap(commandBuffer), cont);
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);

		ObjDisp(commandBuffer)->CmdNextSubpass(Unwrap(commandBuffer), cont);

		// track while reading, for fetching the right set of outputs in AddDrawcall
		m_BakedCmdBufferInfo[m_LastCmdBufferID].state.subpass++;

		const string desc = localSerialiser->GetDebugStr();

		AddEvent(NEXT_SUBPASS, desc);
		FetchDrawcall draw;
		draw.name = StringFormat::Fmt("vkCmdNextSubpass() => %u", m_RenderState.subpass);
		draw.flags |= eDraw_PassBoundary|eDraw_BeginPass|eDraw_EndPass;

		AddDrawcall(draw, true);
	}

	return true;
}

void WrappedVulkan::vkCmdNextSubpass(
	VkCommandBuffer                          commandBuffer,
	VkSubpassContents                        contents)
{
	ObjDisp(commandBuffer)->CmdNextSubpass(Unwrap(commandBuffer), contents);

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(NEXT_SUBPASS);
		Serialise_vkCmdNextSubpass(localSerialiser, commandBuffer, contents);

		record->AddChunk(scope.Get());
	}
}

bool WrappedVulkan::Serialise_vkCmdExecuteCommands(
	Serialiser*                                 localSerialiser,
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    commandBufferCount,
	const VkCommandBuffer*                      pCmdBuffers)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(uint32_t, count, commandBufferCount);
	
	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	vector<ResourceId> cmdids;
	vector<VkCommandBuffer> cmds;

	for(uint32_t i=0; i < count; i++)
	{
		ResourceId id;
		if(m_State >= WRITING)
			id = GetRecord(pCmdBuffers[i])->bakedCommands->GetResourceID();

		localSerialiser->Serialise("pCmdBuffers[]", id);

		if(m_State < WRITING)
		{
			cmdids.push_back(id);
			cmds.push_back(Unwrap(GetResourceManager()->GetLiveHandle<VkCommandBuffer>(id)));
		}
	}

	if(m_State == EXECUTING)
	{
		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);
			
			ObjDisp(commandBuffer)->CmdExecuteCommands(Unwrap(commandBuffer), count, &cmds[0]);
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);

		ObjDisp(commandBuffer)->CmdExecuteCommands(Unwrap(commandBuffer), count, &cmds[0]);

		const string desc = localSerialiser->GetDebugStr();

		AddEvent(EXEC_CMDS, desc);
		FetchDrawcall draw;
		draw.name = "vkCmdExecuteCommands()";
		draw.flags |= eDraw_CmdList;

		AddDrawcall(draw, true);
	}

	return true;
}

void WrappedVulkan::vkCmdExecuteCommands(
	VkCommandBuffer                                 commandBuffer,
	uint32_t                                    commandBufferCount,
	const VkCommandBuffer*                          pCmdBuffers)
{
	VkCommandBuffer *unwrapped = GetTempArray<VkCommandBuffer>(commandBufferCount);
	for(uint32_t i=0; i < commandBufferCount; i++) unwrapped[i] = Unwrap(pCmdBuffers[i]);
	ObjDisp(commandBuffer)->CmdExecuteCommands(Unwrap(commandBuffer), commandBufferCount, unwrapped);

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(EXEC_CMDS);
		Serialise_vkCmdExecuteCommands(localSerialiser, commandBuffer, commandBufferCount, pCmdBuffers);

		record->AddChunk(scope.Get());
		
		for(uint32_t i=0; i < commandBufferCount; i++)
		{
			VkResourceRecord *execRecord = GetRecord(pCmdBuffers[i]);
			record->cmdInfo->dirtied.insert(execRecord->bakedCommands->cmdInfo->dirtied.begin(), execRecord->bakedCommands->cmdInfo->dirtied.end());
			record->cmdInfo->boundDescSets.insert(execRecord->bakedCommands->cmdInfo->boundDescSets.begin(), execRecord->bakedCommands->cmdInfo->boundDescSets.end());
			record->cmdInfo->subcmds.push_back(execRecord);

			GetResourceManager()->MergeBarriers(record->cmdInfo->imgbarriers, execRecord->bakedCommands->cmdInfo->imgbarriers);
		}
	}
}

bool WrappedVulkan::Serialise_vkCmdEndRenderPass(
			Serialiser*                                 localSerialiser,
			VkCommandBuffer                                 commandBuffer)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));

	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	if(m_State == EXECUTING)
	{
		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);

			m_PartialReplayData.renderPassActive = false;
			ObjDisp(commandBuffer)->CmdEndRenderPass(Unwrap(commandBuffer));
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);

		ObjDisp(commandBuffer)->CmdEndRenderPass(Unwrap(commandBuffer));
		
		const string desc = localSerialiser->GetDebugStr();
		
		string opDesc = MakeRenderPassOpString(true);

		AddEvent(END_RENDERPASS, desc);
		FetchDrawcall draw;
		draw.name = StringFormat::Fmt("vkCmdEndRenderPass(%s)", opDesc.c_str());
		draw.flags |= eDraw_PassBoundary|eDraw_EndPass;

		AddDrawcall(draw, true);

		// track while reading, reset this to empty so AddDrawcall sets no outputs,
		// but only AFTER the above AddDrawcall (we want it grouped together)
		m_BakedCmdBufferInfo[m_LastCmdBufferID].state.renderPass = ResourceId();
		m_BakedCmdBufferInfo[m_LastCmdBufferID].state.framebuffer = ResourceId();
	}

	return true;
}

void WrappedVulkan::vkCmdEndRenderPass(
			VkCommandBuffer                                 commandBuffer)
{
	ObjDisp(commandBuffer)->CmdEndRenderPass(Unwrap(commandBuffer));

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(END_RENDERPASS);
		Serialise_vkCmdEndRenderPass(localSerialiser, commandBuffer);

		record->AddChunk(scope.Get());
	}
}

bool WrappedVulkan::Serialise_vkCmdBindPipeline(
			Serialiser*                                 localSerialiser,
			VkCommandBuffer                                 commandBuffer,
			VkPipelineBindPoint                         pipelineBindPoint,
			VkPipeline                                  pipeline)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(VkPipelineBindPoint, bind, pipelineBindPoint);
	SERIALISE_ELEMENT(ResourceId, pipeid, GetResID(pipeline));

	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	if(m_State == EXECUTING)
	{
		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			pipeline = GetResourceManager()->GetLiveHandle<VkPipeline>(pipeid);
			commandBuffer = RerecordCmdBuf(cmdid);

			ObjDisp(commandBuffer)->CmdBindPipeline(Unwrap(commandBuffer), bind, Unwrap(pipeline));

			ResourceId liveid = GetResID(pipeline);

			if(bind == VK_PIPELINE_BIND_POINT_GRAPHICS)
				m_RenderState.graphics.pipeline = liveid;
			else
				m_RenderState.compute.pipeline = liveid;

			if(!m_CreationInfo.m_Pipeline[liveid].dynamicStates[VK_DYNAMIC_STATE_VIEWPORT])
			{
				m_RenderState.views = m_CreationInfo.m_Pipeline[liveid].viewports;
			}
			if(!m_CreationInfo.m_Pipeline[liveid].dynamicStates[VK_DYNAMIC_STATE_SCISSOR])
			{
				m_RenderState.scissors = m_CreationInfo.m_Pipeline[liveid].scissors;
			}
			if(!m_CreationInfo.m_Pipeline[liveid].dynamicStates[VK_DYNAMIC_STATE_LINE_WIDTH])
			{
				m_RenderState.lineWidth = m_CreationInfo.m_Pipeline[liveid].lineWidth;
			}
			if(!m_CreationInfo.m_Pipeline[liveid].dynamicStates[VK_DYNAMIC_STATE_DEPTH_BIAS])
			{
				m_RenderState.bias.depth = m_CreationInfo.m_Pipeline[liveid].depthBiasConstantFactor;
				m_RenderState.bias.biasclamp = m_CreationInfo.m_Pipeline[liveid].depthBiasClamp;
				m_RenderState.bias.slope = m_CreationInfo.m_Pipeline[liveid].depthBiasSlopeFactor;
			}
			if(!m_CreationInfo.m_Pipeline[liveid].dynamicStates[VK_DYNAMIC_STATE_BLEND_CONSTANTS])
			{
				memcpy(m_RenderState.blendConst, m_CreationInfo.m_Pipeline[liveid].blendConst, sizeof(float)*4);
			}
			if(!m_CreationInfo.m_Pipeline[liveid].dynamicStates[VK_DYNAMIC_STATE_DEPTH_BOUNDS])
			{
				m_RenderState.mindepth = m_CreationInfo.m_Pipeline[liveid].minDepthBounds;
				m_RenderState.maxdepth = m_CreationInfo.m_Pipeline[liveid].maxDepthBounds;
			}
			if(!m_CreationInfo.m_Pipeline[liveid].dynamicStates[VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK])
			{
				m_RenderState.front.compare = m_CreationInfo.m_Pipeline[liveid].front.compareMask;
				m_RenderState.back.compare = m_CreationInfo.m_Pipeline[liveid].back.compareMask;
			}
			if(!m_CreationInfo.m_Pipeline[liveid].dynamicStates[VK_DYNAMIC_STATE_STENCIL_WRITE_MASK])
			{
				m_RenderState.front.write = m_CreationInfo.m_Pipeline[liveid].front.writeMask;
				m_RenderState.back.write = m_CreationInfo.m_Pipeline[liveid].back.writeMask;
			}
			if(!m_CreationInfo.m_Pipeline[liveid].dynamicStates[VK_DYNAMIC_STATE_STENCIL_REFERENCE])
			{
				m_RenderState.front.ref = m_CreationInfo.m_Pipeline[liveid].front.reference;
				m_RenderState.back.ref = m_CreationInfo.m_Pipeline[liveid].back.reference;
			}
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);
		pipeline = GetResourceManager()->GetLiveHandle<VkPipeline>(pipeid);

		// track while reading, as we need to bind current topology & index byte width in AddDrawcall
		m_BakedCmdBufferInfo[m_LastCmdBufferID].state.pipeline = GetResID(pipeline);

		ObjDisp(commandBuffer)->CmdBindPipeline(Unwrap(commandBuffer), bind, Unwrap(pipeline));
	}

	return true;
}

void WrappedVulkan::vkCmdBindPipeline(
			VkCommandBuffer                                 commandBuffer,
			VkPipelineBindPoint                         pipelineBindPoint,
			VkPipeline                                  pipeline)
{
	ObjDisp(commandBuffer)->CmdBindPipeline(Unwrap(commandBuffer), pipelineBindPoint, Unwrap(pipeline));

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(BIND_PIPELINE);
		Serialise_vkCmdBindPipeline(localSerialiser, commandBuffer, pipelineBindPoint, pipeline);

		record->AddChunk(scope.Get());
		record->MarkResourceFrameReferenced(GetResID(pipeline), eFrameRef_Read);
	}
}

bool WrappedVulkan::Serialise_vkCmdBindDescriptorSets(
			Serialiser*                                 localSerialiser,
			VkCommandBuffer                                 commandBuffer,
			VkPipelineBindPoint                         pipelineBindPoint,
			VkPipelineLayout                            layout,
			uint32_t                                    firstSet,
			uint32_t                                    setCount,
			const VkDescriptorSet*                      pDescriptorSets,
			uint32_t                                    dynamicOffsetCount,
			const uint32_t*                             pDynamicOffsets)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(ResourceId, layoutid, GetResID(layout));
	SERIALISE_ELEMENT(VkPipelineBindPoint, bind, pipelineBindPoint);
	SERIALISE_ELEMENT(uint32_t, first, firstSet);

	SERIALISE_ELEMENT(uint32_t, numSets, setCount);

	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	ResourceId *descriptorIDs = NULL;

	VkDescriptorSet *sets = (VkDescriptorSet *)pDescriptorSets;
	if(m_State < WRITING)
	{
		descriptorIDs = new ResourceId[numSets];
		sets = new VkDescriptorSet[numSets];
	}

	for(uint32_t i=0; i < numSets; i++)
	{
		ResourceId id;
		if(m_State >= WRITING)
			id = GetResID(sets[i]);

		localSerialiser->Serialise("DescriptorSet", id);

		if(m_State < WRITING)
		{
			descriptorIDs[i] = id;
			sets[i] = GetResourceManager()->GetLiveHandle<VkDescriptorSet>(descriptorIDs[i]);
			descriptorIDs[i] = GetResID(sets[i]);
			sets[i] = Unwrap(sets[i]);
		}
	}

	SERIALISE_ELEMENT(uint32_t, offsCount, dynamicOffsetCount);
	SERIALISE_ELEMENT_ARR_OPT(uint32_t, offs, pDynamicOffsets, offsCount, offsCount > 0);

	if(m_State == EXECUTING)
	{
		layout = GetResourceManager()->GetLiveHandle<VkPipelineLayout>(layoutid);

		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);

			ObjDisp(commandBuffer)->CmdBindDescriptorSets(Unwrap(commandBuffer), bind, Unwrap(layout), first, numSets, sets, offsCount, offs);

			vector<ResourceId> &descsets =
				(bind == VK_PIPELINE_BIND_POINT_GRAPHICS)
				? m_RenderState.graphics.descSets
				: m_RenderState.compute.descSets;
			
			vector< vector<uint32_t> > &offsets =
				(bind == VK_PIPELINE_BIND_POINT_GRAPHICS)
				? m_RenderState.graphics.offsets
				: m_RenderState.compute.offsets;

			// expand as necessary
			if(descsets.size() < first + numSets)
			{
				descsets.resize(first + numSets);
				offsets.resize(first + numSets);
			}

			const vector<ResourceId> &descSetLayouts = m_CreationInfo.m_PipelineLayout[GetResID(layout)].descSetLayouts;

			uint32_t *offsIter = offs;
			uint32_t dynConsumed = 0;

			// consume the offsets linearly along the descriptor set layouts
			for(uint32_t i=0; i < numSets; i++)
			{
				descsets[first+i] = descriptorIDs[i];
				uint32_t dynCount = m_CreationInfo.m_DescSetLayout[ descSetLayouts[first+i] ].dynamicCount;
				offsets[first+i].assign(offsIter, offsIter+dynCount);
				dynConsumed += dynCount;
				RDCASSERT(dynConsumed <= offsCount);
			}

			// if there are dynamic offsets, bake them into the current bindings by alias'ing
			// the image layout member (which is never used for buffer views).
			// This lets us look it up easily when we want to show the current pipeline state
			RDCCOMPILE_ASSERT(sizeof(VkImageLayout) >= sizeof(uint32_t), "Can't alias image layout for dynamic offset!");
			if(offsCount > 0)
			{
				uint32_t o = 0;

				// spec states that dynamic offsets precisely match all the offsets needed for these
				// sets, in order of set N before set N+1, binding X before binding X+1 within a set,
				// and in array element order within a binding
				for(uint32_t i=0; i < numSets; i++)
				{
					const DescSetLayout &layoutinfo = m_CreationInfo.m_DescSetLayout[ descSetLayouts[first+i] ];

					for(size_t b=0; b < layoutinfo.bindings.size(); b++)
					{
						// not dynamic, doesn't need an offset
						if(layoutinfo.bindings[b].descriptorType != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC &&
							 layoutinfo.bindings[b].descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
							 continue;

						// assign every array element an offset according to array size
						for(uint32_t a=0; a < layoutinfo.bindings[b].descriptorCount; a++)
						{
							RDCASSERT(o < offsCount);
							uint32_t *alias = (uint32_t *)&m_DescriptorSetState[descriptorIDs[i]].currentBindings[b][a].imageInfo.imageLayout;
							*alias = offs[o++];
						}
					}
				}
			}
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);
		layout = GetResourceManager()->GetLiveHandle<VkPipelineLayout>(layoutid);
		
		// track while reading, as we need to track resource usage
		vector<ResourceId> &descsets =
			(bind == VK_PIPELINE_BIND_POINT_GRAPHICS)
			? m_BakedCmdBufferInfo[m_LastCmdBufferID].state.graphicsDescSets
			: m_BakedCmdBufferInfo[m_LastCmdBufferID].state.computeDescSets;

		// expand as necessary
		if(descsets.size() < first + numSets)
			descsets.resize(first + numSets);

		for(uint32_t i=0; i < numSets; i++)
			descsets[first+i] = descriptorIDs[i];

		ObjDisp(commandBuffer)->CmdBindDescriptorSets(Unwrap(commandBuffer), bind, Unwrap(layout), first, numSets, sets, offsCount, offs);
	}

	if(m_State < WRITING)
	{
		SAFE_DELETE_ARRAY(sets);
		SAFE_DELETE_ARRAY(descriptorIDs);
	}

	SAFE_DELETE_ARRAY(offs);

	return true;
}

void WrappedVulkan::vkCmdBindDescriptorSets(
			VkCommandBuffer                                 commandBuffer,
			VkPipelineBindPoint                         pipelineBindPoint,
			VkPipelineLayout                            layout,
			uint32_t                                    firstSet,
			uint32_t                                    setCount,
			const VkDescriptorSet*                      pDescriptorSets,
			uint32_t                                    dynamicOffsetCount,
			const uint32_t*                             pDynamicOffsets)
{
	VkDescriptorSet *unwrapped = GetTempArray<VkDescriptorSet>(setCount);
	for(uint32_t i=0; i < setCount; i++) unwrapped[i] = Unwrap(pDescriptorSets[i]);

	ObjDisp(commandBuffer)->CmdBindDescriptorSets(Unwrap(commandBuffer), pipelineBindPoint, Unwrap(layout), firstSet, setCount, unwrapped, dynamicOffsetCount, pDynamicOffsets);

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(BIND_DESCRIPTOR_SET);
		Serialise_vkCmdBindDescriptorSets(localSerialiser, commandBuffer, pipelineBindPoint, layout, firstSet, setCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);

		record->AddChunk(scope.Get());
		record->MarkResourceFrameReferenced(GetResID(layout), eFrameRef_Read);
		record->cmdInfo->boundDescSets.insert(pDescriptorSets, pDescriptorSets + setCount);
	}
}

bool WrappedVulkan::Serialise_vkCmdBindVertexBuffers(
		Serialiser*                                 localSerialiser,
    VkCommandBuffer                                 commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(uint32_t, first, firstBinding);
	SERIALISE_ELEMENT(uint32_t, count, bindingCount);

	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	vector<ResourceId> bufids;
	vector<VkBuffer> bufs;
	vector<VkDeviceSize> offs;

	for(uint32_t i=0; i < count; i++)
	{
		ResourceId id;
		VkDeviceSize o;
		if(m_State >= WRITING)
		{
			id = GetResID(pBuffers[i]);
			o = pOffsets[i];
		}

		localSerialiser->Serialise("pBuffers[]", id);
		localSerialiser->Serialise("pOffsets[]", o);

		if(m_State < WRITING)
		{
			VkBuffer buf = GetResourceManager()->GetLiveHandle<VkBuffer>(id);
			bufids.push_back(GetResID(buf));
			bufs.push_back(Unwrap(buf));
			offs.push_back(o);
		}
	}

	if(m_State == EXECUTING)
	{
		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);
			ObjDisp(commandBuffer)->CmdBindVertexBuffers(Unwrap(commandBuffer), first, count, &bufs[0], &offs[0]);

			if(m_RenderState.vbuffers.size() < first + count)
				m_RenderState.vbuffers.resize(first + count);

			for(uint32_t i=0; i < count; i++)
			{
				m_RenderState.vbuffers[first + i].buf = bufids[i];
				m_RenderState.vbuffers[first + i].offs = offs[i];
			}
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);
		
		// track while reading, as we need to track resource usage
		if(m_BakedCmdBufferInfo[m_LastCmdBufferID].state.vbuffers.size() < first + count)
			m_BakedCmdBufferInfo[m_LastCmdBufferID].state.vbuffers.resize(first + count);

		for(uint32_t i=0; i < count; i++)
			m_BakedCmdBufferInfo[m_LastCmdBufferID].state.vbuffers[first + i] = bufids[i];

		ObjDisp(commandBuffer)->CmdBindVertexBuffers(Unwrap(commandBuffer), first, count, &bufs[0], &offs[0]);
	}

	return true;
}

void WrappedVulkan::vkCmdBindVertexBuffers(
    VkCommandBuffer                                 commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets)
{
	VkBuffer *unwrapped = GetTempArray<VkBuffer>(bindingCount);
	for(uint32_t i=0; i < bindingCount; i++) unwrapped[i] = Unwrap(pBuffers[i]);

	ObjDisp(commandBuffer)->CmdBindVertexBuffers(Unwrap(commandBuffer), firstBinding, bindingCount, unwrapped, pOffsets);

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(BIND_VERTEX_BUFFERS);
		Serialise_vkCmdBindVertexBuffers(localSerialiser, commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);

		record->AddChunk(scope.Get());
		for(uint32_t i=0; i < bindingCount; i++)
		{
			record->MarkResourceFrameReferenced(GetResID(pBuffers[i]), eFrameRef_Read);
			record->MarkResourceFrameReferenced(GetRecord(pBuffers[i])->baseResource, eFrameRef_Read);
			if(GetRecord(pBuffers[i])->sparseInfo)
				record->cmdInfo->sparse.insert(GetRecord(pBuffers[i])->sparseInfo);
		}
	}
}


bool WrappedVulkan::Serialise_vkCmdBindIndexBuffer(
		Serialiser*                                 localSerialiser,
    VkCommandBuffer                                 commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkIndexType                                 indexType)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(ResourceId, bufid, GetResID(buffer));
	SERIALISE_ELEMENT(uint64_t, offs, offset);
	SERIALISE_ELEMENT(VkIndexType, idxType, indexType);

	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	if(m_State == EXECUTING)
	{
		buffer = GetResourceManager()->GetLiveHandle<VkBuffer>(bufid);

		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);
			ObjDisp(commandBuffer)->CmdBindIndexBuffer(Unwrap(commandBuffer), Unwrap(buffer), offs, idxType);

			m_RenderState.ibuffer.buf = GetResID(buffer);
			m_RenderState.ibuffer.offs = offs;
			m_RenderState.ibuffer.bytewidth = idxType == VK_INDEX_TYPE_UINT32 ? 4 : 2;
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);
		buffer = GetResourceManager()->GetLiveHandle<VkBuffer>(bufid);

		// track while reading, as we need to bind current topology & index byte width in AddDrawcall
		m_BakedCmdBufferInfo[m_LastCmdBufferID].state.idxWidth = (idxType == VK_INDEX_TYPE_UINT32 ? 4 : 2);
		
		// track while reading, as we need to track resource usage
		m_BakedCmdBufferInfo[m_LastCmdBufferID].state.ibuffer = GetResID(buffer);

		ObjDisp(commandBuffer)->CmdBindIndexBuffer(Unwrap(commandBuffer), Unwrap(buffer), offs, idxType);
	}

	return true;
}

void WrappedVulkan::vkCmdBindIndexBuffer(
    VkCommandBuffer                                 commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkIndexType                                 indexType)
{
	ObjDisp(commandBuffer)->CmdBindIndexBuffer(Unwrap(commandBuffer), Unwrap(buffer), offset, indexType);

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(BIND_INDEX_BUFFER);
		Serialise_vkCmdBindIndexBuffer(localSerialiser, commandBuffer, buffer, offset, indexType);

		record->AddChunk(scope.Get());
		record->MarkResourceFrameReferenced(GetResID(buffer), eFrameRef_Read);
		record->MarkResourceFrameReferenced(GetRecord(buffer)->baseResource, eFrameRef_Read);
		if(GetRecord(buffer)->sparseInfo)
			record->cmdInfo->sparse.insert(GetRecord(buffer)->sparseInfo);
	}
}

bool WrappedVulkan::Serialise_vkCmdUpdateBuffer(
	Serialiser*                                 localSerialiser,
	VkCommandBuffer                                 commandBuffer,
	VkBuffer                                    destBuffer,
	VkDeviceSize                                destOffset,
	VkDeviceSize                                dataSize,
	const uint32_t*                             pData)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(ResourceId, bufid, GetResID(destBuffer));
	SERIALISE_ELEMENT(VkDeviceSize, offs, destOffset);
	SERIALISE_ELEMENT(VkDeviceSize, sz, dataSize);
	SERIALISE_ELEMENT_BUF(byte *, bufdata, (byte *)pData, (size_t)dataSize);

	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	if(m_State == EXECUTING)
	{
		destBuffer = GetResourceManager()->GetLiveHandle<VkBuffer>(bufid);

		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);
			ObjDisp(commandBuffer)->CmdUpdateBuffer(Unwrap(commandBuffer), Unwrap(destBuffer), offs, sz, (uint32_t *)bufdata);
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);
		destBuffer = GetResourceManager()->GetLiveHandle<VkBuffer>(bufid);

		ObjDisp(commandBuffer)->CmdUpdateBuffer(Unwrap(commandBuffer), Unwrap(destBuffer), offs, sz, (uint32_t *)bufdata);
	}

	if(m_State < WRITING)
		SAFE_DELETE_ARRAY(bufdata);

	return true;
}

void WrappedVulkan::vkCmdUpdateBuffer(
	VkCommandBuffer                                 commandBuffer,
	VkBuffer                                    destBuffer,
	VkDeviceSize                                destOffset,
	VkDeviceSize                                dataSize,
	const uint32_t*                             pData)
{
	ObjDisp(commandBuffer)->CmdUpdateBuffer(Unwrap(commandBuffer), Unwrap(destBuffer), destOffset, dataSize, pData);

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(UPDATE_BUF);
		Serialise_vkCmdUpdateBuffer(localSerialiser, commandBuffer, destBuffer, destOffset, dataSize, pData);

		record->AddChunk(scope.Get());

		VkResourceRecord *buf = GetRecord(destBuffer);

		// mark buffer just as read, and memory behind as write & dirtied
		record->MarkResourceFrameReferenced(buf->GetResourceID(), eFrameRef_Read);
		record->MarkResourceFrameReferenced(buf->baseResource, eFrameRef_Write);
		if(buf->baseResource != ResourceId())
			record->cmdInfo->dirtied.insert(buf->baseResource);
		if(buf->sparseInfo)
			record->cmdInfo->sparse.insert(buf->sparseInfo);
	}
}

bool WrappedVulkan::Serialise_vkCmdFillBuffer(
	Serialiser*                                 localSerialiser,
	VkCommandBuffer                                 commandBuffer,
	VkBuffer                                    destBuffer,
	VkDeviceSize                                destOffset,
	VkDeviceSize                                fillSize,
	uint32_t                                    data)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(ResourceId, bufid, GetResID(destBuffer));
	SERIALISE_ELEMENT(VkDeviceSize, offs, destOffset);
	SERIALISE_ELEMENT(VkDeviceSize, sz, fillSize);
	SERIALISE_ELEMENT(uint32_t, d, data);

	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	if(m_State == EXECUTING)
	{
		destBuffer = GetResourceManager()->GetLiveHandle<VkBuffer>(bufid);

		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);
			ObjDisp(commandBuffer)->CmdFillBuffer(Unwrap(commandBuffer), Unwrap(destBuffer), offs, sz, d);
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);
		destBuffer = GetResourceManager()->GetLiveHandle<VkBuffer>(bufid);

		ObjDisp(commandBuffer)->CmdFillBuffer(Unwrap(commandBuffer), Unwrap(destBuffer), offs, sz, d);
	}

	return true;
}

void WrappedVulkan::vkCmdFillBuffer(
	VkCommandBuffer                                 commandBuffer,
	VkBuffer                                    destBuffer,
	VkDeviceSize                                destOffset,
	VkDeviceSize                                fillSize,
	uint32_t                                    data)
{
	ObjDisp(commandBuffer)->CmdFillBuffer(Unwrap(commandBuffer), Unwrap(destBuffer), destOffset, fillSize, data);

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(FILL_BUF);
		Serialise_vkCmdFillBuffer(localSerialiser, commandBuffer, destBuffer, destOffset, fillSize, data);

		record->AddChunk(scope.Get());

		VkResourceRecord *buf = GetRecord(destBuffer);

		// mark buffer just as read, and memory behind as write & dirtied
		record->MarkResourceFrameReferenced(buf->GetResourceID(), eFrameRef_Read);
		record->MarkResourceFrameReferenced(buf->baseResource, eFrameRef_Write);
		if(buf->baseResource != ResourceId())
			record->cmdInfo->dirtied.insert(buf->baseResource);
		if(buf->sparseInfo)
			record->cmdInfo->sparse.insert(buf->sparseInfo);
	}
}

bool WrappedVulkan::Serialise_vkCmdPushConstants(
	Serialiser*                                 localSerialiser,
	VkCommandBuffer                                 commandBuffer,
	VkPipelineLayout                            layout,
	VkShaderStageFlags                          stageFlags,
	uint32_t                                    start,
	uint32_t                                    length,
	const void*                                 values)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(ResourceId, layid, GetResID(layout));
	SERIALISE_ELEMENT(VkShaderStageFlagBits, flags, (VkShaderStageFlagBits)stageFlags);
	SERIALISE_ELEMENT(uint32_t, s, start);
	SERIALISE_ELEMENT(uint32_t, len, length);
	SERIALISE_ELEMENT_BUF(byte *, vals, (byte *)values, (size_t)len);

	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	if(m_State == EXECUTING)
	{
		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);
			layout = GetResourceManager()->GetLiveHandle<VkPipelineLayout>(layid);
			ObjDisp(commandBuffer)->CmdPushConstants(Unwrap(commandBuffer), Unwrap(layout), flags, s, len, vals);

			RDCASSERT(s+len < (uint32_t)ARRAY_COUNT(m_RenderState.pushconsts));

			memcpy(m_RenderState.pushconsts + s, vals, len);
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);
		layout = GetResourceManager()->GetLiveHandle<VkPipelineLayout>(layid);

		ObjDisp(commandBuffer)->CmdPushConstants(Unwrap(commandBuffer), Unwrap(layout), flags, s, len, vals);
	}

	if(m_State < WRITING)
		SAFE_DELETE_ARRAY(vals);

	return true;
}

void WrappedVulkan::vkCmdPushConstants(
	VkCommandBuffer                                 commandBuffer,
	VkPipelineLayout                            layout,
	VkShaderStageFlags                          stageFlags,
	uint32_t                                    start,
	uint32_t                                    length,
	const void*                                 values)
{
	ObjDisp(commandBuffer)->CmdPushConstants(Unwrap(commandBuffer), Unwrap(layout), stageFlags, start, length, values);

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(PUSH_CONST);
		Serialise_vkCmdPushConstants(localSerialiser, commandBuffer, layout, stageFlags, start, length, values);

		record->AddChunk(scope.Get());
	}
}

bool WrappedVulkan::Serialise_vkCmdPipelineBarrier(
			Serialiser*                                 localSerialiser,
			VkCommandBuffer                                 commandBuffer,
			VkPipelineStageFlags                        srcStageMask,
			VkPipelineStageFlags                        destStageMask,
			VkDependencyFlags                           dependencyFlags,
			uint32_t                                    memoryBarrierCount,
			const VkMemoryBarrier*                      pMemoryBarriers,
			uint32_t                                    bufferMemoryBarrierCount,
			const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
			uint32_t                                    imageMemoryBarrierCount,
			const VkImageMemoryBarrier*                 pImageMemoryBarriers)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(VkPipelineStageFlagBits, srcStages, (VkPipelineStageFlagBits)srcStageMask);
	SERIALISE_ELEMENT(VkPipelineStageFlagBits, destStages, (VkPipelineStageFlagBits)destStageMask);
	
	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	SERIALISE_ELEMENT(VkDependencyFlags, flags, dependencyFlags);

	SERIALISE_ELEMENT(uint32_t, memCount, memoryBarrierCount);
	SERIALISE_ELEMENT(uint32_t, bufCount, bufferMemoryBarrierCount);
	SERIALISE_ELEMENT(uint32_t, imgCount, imageMemoryBarrierCount);

	SERIALISE_ELEMENT_ARR(VkMemoryBarrier, memBarriers, pMemoryBarriers, memCount);
	SERIALISE_ELEMENT_ARR(VkBufferMemoryBarrier, bufMemBarriers, pBufferMemoryBarriers, bufCount);
	SERIALISE_ELEMENT_ARR(VkImageMemoryBarrier, imgMemBarriers, pImageMemoryBarriers, imgCount);

	vector<VkImageMemoryBarrier> imgBarriers;
	vector<VkBufferMemoryBarrier> bufBarriers;

	// it's possible for buffer or image to be NULL if it refers to a resource that is otherwise
	// not in the log (barriers do not mark resources referenced). If the resource in question does
	// not exist, then it's safe to skip this barrier.
	
	if(m_State < WRITING)
	{
		for(uint32_t i=0; i < bufCount; i++)
			if(bufMemBarriers[i].buffer != VK_NULL_HANDLE)
				bufBarriers.push_back(bufMemBarriers[i]);
		
		for(uint32_t i=0; i < imgCount; i++)
		{
			if(imgMemBarriers[i].image != VK_NULL_HANDLE)
			{
				imgBarriers.push_back(imgMemBarriers[i]);
				ReplacePresentableImageLayout(imgBarriers.back().oldLayout);
				ReplacePresentableImageLayout(imgBarriers.back().newLayout);
			}
		}
	}

	SAFE_DELETE_ARRAY(bufMemBarriers);
	SAFE_DELETE_ARRAY(imgMemBarriers);
	
	if(m_State == EXECUTING)
	{
		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);
			ObjDisp(commandBuffer)->CmdPipelineBarrier(Unwrap(commandBuffer), (VkPipelineStageFlags)srcStages, (VkPipelineStageFlags)destStages, flags,
				memCount, memBarriers,
				(uint32_t)bufBarriers.size(), &bufBarriers[0],
				(uint32_t)imgBarriers.size(), &imgBarriers[0]);

			ResourceId cmd = GetResID(RerecordCmdBuf(cmdid));
			GetResourceManager()->RecordBarriers(m_BakedCmdBufferInfo[cmd].imgbarriers, m_ImageLayouts, (uint32_t)imgBarriers.size(), &imgBarriers[0]);
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);

		ObjDisp(commandBuffer)->CmdPipelineBarrier(Unwrap(commandBuffer), (VkPipelineStageFlags)srcStages, (VkPipelineStageFlags)destStages, flags,
				memCount, memBarriers,
				(uint32_t)bufBarriers.size(), &bufBarriers[0],
				(uint32_t)imgBarriers.size(), &imgBarriers[0]);
		
		ResourceId cmd = GetResID(commandBuffer);
		GetResourceManager()->RecordBarriers(m_BakedCmdBufferInfo[cmd].imgbarriers, m_ImageLayouts, (uint32_t)imgBarriers.size(), &imgBarriers[0]);
	}

	SAFE_DELETE_ARRAY(memBarriers);

	return true;
}

void WrappedVulkan::vkCmdPipelineBarrier(
			VkCommandBuffer                             commandBuffer,
			VkPipelineStageFlags                        srcStageMask,
			VkPipelineStageFlags                        destStageMask,
			VkDependencyFlags                           dependencyFlags,
			uint32_t                                    memoryBarrierCount,
			const VkMemoryBarrier*                      pMemoryBarriers,
			uint32_t                                    bufferMemoryBarrierCount,
			const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
			uint32_t                                    imageMemoryBarrierCount,
			const VkImageMemoryBarrier*                 pImageMemoryBarriers)
{

	{
		byte *memory = GetTempMemory(sizeof(VkBufferMemoryBarrier)*bufferMemoryBarrierCount + sizeof(VkImageMemoryBarrier)*imageMemoryBarrierCount);

		VkImageMemoryBarrier *im = (VkImageMemoryBarrier *)memory;
		VkBufferMemoryBarrier *buf = (VkBufferMemoryBarrier *)(im + imageMemoryBarrierCount);

		for(uint32_t i=0; i < bufferMemoryBarrierCount; i++)
		{
			buf[i] = pBufferMemoryBarriers[i];
			buf[i].buffer = Unwrap(buf[i].buffer);
		}

		for(uint32_t i=0; i < imageMemoryBarrierCount; i++)
		{
			im[i] = pImageMemoryBarriers[i];
			im[i].image = Unwrap(im[i].image);
		}

		ObjDisp(commandBuffer)->CmdPipelineBarrier(Unwrap(commandBuffer), srcStageMask, destStageMask, dependencyFlags,
			memoryBarrierCount, pMemoryBarriers,
			bufferMemoryBarrierCount, buf,
			imageMemoryBarrierCount, im);
	}

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(PIPELINE_BARRIER);
		Serialise_vkCmdPipelineBarrier(localSerialiser, commandBuffer, srcStageMask, destStageMask, dependencyFlags,
			memoryBarrierCount, pMemoryBarriers,
			bufferMemoryBarrierCount, pBufferMemoryBarriers,
			imageMemoryBarrierCount, pImageMemoryBarriers);

		record->AddChunk(scope.Get());
		
		if(imageMemoryBarrierCount > 0)
		{
			SCOPED_LOCK(m_ImageLayoutsLock);
			GetResourceManager()->RecordBarriers(GetRecord(commandBuffer)->cmdInfo->imgbarriers, m_ImageLayouts, imageMemoryBarrierCount, pImageMemoryBarriers);
		}
	}
}

bool WrappedVulkan::Serialise_vkCmdWriteTimestamp(
		Serialiser*                                 localSerialiser,
		VkCommandBuffer                             commandBuffer,
		VkPipelineStageFlagBits                     pipelineStage,
		VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(VkPipelineStageFlagBits, stage, pipelineStage);
	SERIALISE_ELEMENT(ResourceId, poolid, GetResID(queryPool));
	SERIALISE_ELEMENT(uint32_t, q, query);

	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;

	if(m_State == EXECUTING)
	{
		queryPool = GetResourceManager()->GetLiveHandle<VkQueryPool>(poolid);

		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);
			ObjDisp(commandBuffer)->CmdWriteTimestamp(Unwrap(commandBuffer), stage, Unwrap(queryPool), q);
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);
		queryPool = GetResourceManager()->GetLiveHandle<VkQueryPool>(poolid);

		ObjDisp(commandBuffer)->CmdWriteTimestamp(Unwrap(commandBuffer), stage, Unwrap(queryPool), q);
	}

	return true;
}

void WrappedVulkan::vkCmdWriteTimestamp(
		VkCommandBuffer                             commandBuffer,
		VkPipelineStageFlagBits                     pipelineStage,
		VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
	ObjDisp(commandBuffer)->CmdWriteTimestamp(Unwrap(commandBuffer), pipelineStage, Unwrap(queryPool), query);

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(WRITE_TIMESTAMP);
		Serialise_vkCmdWriteTimestamp(localSerialiser, commandBuffer, pipelineStage, queryPool, query);

		record->AddChunk(scope.Get());
		
		record->MarkResourceFrameReferenced(GetResID(queryPool), eFrameRef_Read);
	}
}

bool WrappedVulkan::Serialise_vkCmdCopyQueryPoolResults(
		Serialiser*                                 localSerialiser,
		VkCommandBuffer                                 commandBuffer,
		VkQueryPool                                 queryPool,
		uint32_t                                    firstQuery,
		uint32_t                                    queryCount,
		VkBuffer                                    destBuffer,
		VkDeviceSize                                destOffset,
		VkDeviceSize                                destStride,
		VkQueryResultFlags                          flags)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(ResourceId, qid, GetResID(queryPool));
	SERIALISE_ELEMENT(uint32_t, first, firstQuery);
	SERIALISE_ELEMENT(uint32_t, count, queryCount);
	SERIALISE_ELEMENT(ResourceId, bufid, GetResID(destBuffer));
	SERIALISE_ELEMENT(VkDeviceSize, offs, destOffset);
	SERIALISE_ELEMENT(VkDeviceSize, stride, destStride);
	SERIALISE_ELEMENT(VkQueryResultFlagBits, f, (VkQueryResultFlagBits)flags);

	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;

	if(m_State == EXECUTING)
	{
		queryPool = GetResourceManager()->GetLiveHandle<VkQueryPool>(qid);
		destBuffer = GetResourceManager()->GetLiveHandle<VkBuffer>(bufid);

		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);
			ObjDisp(commandBuffer)->CmdCopyQueryPoolResults(Unwrap(commandBuffer), Unwrap(queryPool), first, count, Unwrap(destBuffer), offs, stride, f);
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);
		queryPool = GetResourceManager()->GetLiveHandle<VkQueryPool>(qid);
		destBuffer = GetResourceManager()->GetLiveHandle<VkBuffer>(bufid);

		ObjDisp(commandBuffer)->CmdCopyQueryPoolResults(Unwrap(commandBuffer), Unwrap(queryPool), first, count, Unwrap(destBuffer), offs, stride, f);
	}

	return true;
}

void WrappedVulkan::vkCmdCopyQueryPoolResults(
		VkCommandBuffer                                 commandBuffer,
		VkQueryPool                                 queryPool,
		uint32_t                                    firstQuery,
		uint32_t                                    queryCount,
		VkBuffer                                    destBuffer,
		VkDeviceSize                                destOffset,
		VkDeviceSize                                destStride,
		VkQueryResultFlags                          flags)
{
	ObjDisp(commandBuffer)->CmdCopyQueryPoolResults(Unwrap(commandBuffer), Unwrap(queryPool), firstQuery, queryCount, Unwrap(destBuffer), destOffset, destStride, flags);

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(COPY_QUERY_RESULTS);
		Serialise_vkCmdCopyQueryPoolResults(localSerialiser, commandBuffer, queryPool, firstQuery, queryCount, destBuffer, destOffset, destStride, flags);

		record->AddChunk(scope.Get());
		record->MarkResourceFrameReferenced(GetResID(queryPool), eFrameRef_Read);

		VkResourceRecord *buf = GetRecord(destBuffer);

		// mark buffer just as read, and memory behind as write & dirtied
		record->MarkResourceFrameReferenced(buf->GetResourceID(), eFrameRef_Read);
		record->MarkResourceFrameReferenced(buf->baseResource, eFrameRef_Write);
		if(buf->baseResource != ResourceId())
			record->cmdInfo->dirtied.insert(buf->baseResource);
		if(buf->sparseInfo)
			record->cmdInfo->sparse.insert(buf->sparseInfo);
	}
}

bool WrappedVulkan::Serialise_vkCmdBeginQuery(
		Serialiser*                                 localSerialiser,
    VkCommandBuffer                                 commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(ResourceId, qid, GetResID(queryPool));
	SERIALISE_ELEMENT(uint32_t, q, query);
	SERIALISE_ELEMENT(VkQueryControlFlagBits, f, (VkQueryControlFlagBits)flags); // serialise as 'bits' type to get nice enum values

	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	if(m_State == EXECUTING)
	{
		queryPool = GetResourceManager()->GetLiveHandle<VkQueryPool>(qid);

		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);
			ObjDisp(commandBuffer)->CmdBeginQuery(Unwrap(commandBuffer), Unwrap(queryPool), q, f);
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);
		queryPool = GetResourceManager()->GetLiveHandle<VkQueryPool>(qid);
		
		ObjDisp(commandBuffer)->CmdBeginQuery(Unwrap(commandBuffer), Unwrap(queryPool), q, f);
	}

	return true;
}

void WrappedVulkan::vkCmdBeginQuery(
    VkCommandBuffer                                 commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags)
{
	ObjDisp(commandBuffer)->CmdBeginQuery(Unwrap(commandBuffer), Unwrap(queryPool), query, flags);

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(BEGIN_QUERY);
		Serialise_vkCmdBeginQuery(localSerialiser, commandBuffer, queryPool, query, flags);

		record->AddChunk(scope.Get());
		record->MarkResourceFrameReferenced(GetResID(queryPool), eFrameRef_Read);
	}
}

bool WrappedVulkan::Serialise_vkCmdEndQuery(
		Serialiser*                                 localSerialiser,
    VkCommandBuffer                                 commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(ResourceId, qid, GetResID(queryPool));
	SERIALISE_ELEMENT(uint32_t, q, query);

	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	if(m_State == EXECUTING)
	{
		queryPool = GetResourceManager()->GetLiveHandle<VkQueryPool>(qid);

		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);
			ObjDisp(commandBuffer)->CmdEndQuery(Unwrap(commandBuffer), Unwrap(queryPool), q);
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);
		queryPool = GetResourceManager()->GetLiveHandle<VkQueryPool>(qid);
		
		ObjDisp(commandBuffer)->CmdEndQuery(Unwrap(commandBuffer), Unwrap(queryPool), q);
	}

	return true;
}

void WrappedVulkan::vkCmdEndQuery(
    VkCommandBuffer                                 commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
	ObjDisp(commandBuffer)->CmdEndQuery(Unwrap(commandBuffer), Unwrap(queryPool), query);

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(END_QUERY);
		Serialise_vkCmdEndQuery(localSerialiser, commandBuffer, queryPool, query);

		record->AddChunk(scope.Get());
		record->MarkResourceFrameReferenced(GetResID(queryPool), eFrameRef_Read);
	}
}

bool WrappedVulkan::Serialise_vkCmdResetQueryPool(
		Serialiser*                                 localSerialiser,
    VkCommandBuffer                                 commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(ResourceId, qid, GetResID(queryPool));
	SERIALISE_ELEMENT(uint32_t, first, firstQuery);
	SERIALISE_ELEMENT(uint32_t, count, queryCount);

	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	if(m_State == EXECUTING)
	{
		queryPool = GetResourceManager()->GetLiveHandle<VkQueryPool>(qid);

		if(ShouldRerecordCmd(cmdid) && InRerecordRange())
		{
			commandBuffer = RerecordCmdBuf(cmdid);
			ObjDisp(commandBuffer)->CmdResetQueryPool(Unwrap(commandBuffer), Unwrap(queryPool), first, count);
		}
	}
	else if(m_State == READING)
	{
		commandBuffer = GetResourceManager()->GetLiveHandle<VkCommandBuffer>(cmdid);
		queryPool = GetResourceManager()->GetLiveHandle<VkQueryPool>(qid);
		
		ObjDisp(commandBuffer)->CmdResetQueryPool(Unwrap(commandBuffer), Unwrap(queryPool), first, count);
	}

	return true;
}

void WrappedVulkan::vkCmdResetQueryPool(
    VkCommandBuffer                                 commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount)
{
	ObjDisp(commandBuffer)->CmdResetQueryPool(Unwrap(commandBuffer), Unwrap(queryPool), firstQuery, queryCount);

	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(RESET_QUERY_POOL);
		Serialise_vkCmdResetQueryPool(localSerialiser, commandBuffer, queryPool, firstQuery, queryCount);

		record->AddChunk(scope.Get());
		record->MarkResourceFrameReferenced(GetResID(queryPool), eFrameRef_Read);
	}
}

bool WrappedVulkan::Serialise_vkCmdDbgMarkerBegin(
			Serialiser*                                 localSerialiser,
			VkCommandBuffer  commandBuffer,
			const char*     pMarker)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	SERIALISE_ELEMENT(string, name, pMarker ? string(pMarker) : "");
	
	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	if(m_State == READING)
	{
		FetchDrawcall draw;
		draw.name = name;
		draw.flags |= eDraw_PushMarker;

		AddDrawcall(draw, false);
	}

	return true;
}

void WrappedVulkan::vkCmdDbgMarkerBegin(
			VkCommandBuffer  commandBuffer,
			const char*     pMarker)
{
	if(ObjDisp(commandBuffer)->CmdDbgMarkerBegin)
		ObjDisp(commandBuffer)->CmdDbgMarkerBegin(Unwrap(commandBuffer), pMarker);
	
	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(BEGIN_EVENT);
		Serialise_vkCmdDbgMarkerBegin(localSerialiser, commandBuffer, pMarker);

		record->AddChunk(scope.Get());
	}
}

bool WrappedVulkan::Serialise_vkCmdDbgMarkerEnd(Serialiser* localSerialiser, VkCommandBuffer commandBuffer)
{
	SERIALISE_ELEMENT(ResourceId, cmdid, GetResID(commandBuffer));
	
	if(m_State < WRITING)
		m_LastCmdBufferID = cmdid;
	
	if(m_State == READING && !m_BakedCmdBufferInfo[m_LastCmdBufferID].curEvents.empty())
	{
		FetchDrawcall draw;
		draw.name = "API Calls";
		draw.flags = eDraw_SetMarker;

		AddDrawcall(draw, true);
	}
		
	if(m_State == READING)
	{
		// dummy draw that is consumed when this command buffer
		// is being in-lined into the call stream
		FetchDrawcall draw;
		draw.name = "Pop()";
		draw.flags = eDraw_PopMarker;

		AddDrawcall(draw, false);
	}

	return true;
}

void WrappedVulkan::vkCmdDbgMarkerEnd(
	VkCommandBuffer  commandBuffer)
{
	if(ObjDisp(commandBuffer)->CmdDbgMarkerEnd)
		ObjDisp(commandBuffer)->CmdDbgMarkerEnd(Unwrap(commandBuffer));
	
	if(m_State >= WRITING)
	{
		VkResourceRecord *record = GetRecord(commandBuffer);

		CACHE_THREAD_SERIALISER();

		SCOPED_SERIALISE_CONTEXT(END_EVENT);
		Serialise_vkCmdDbgMarkerEnd(localSerialiser, commandBuffer);

		record->AddChunk(scope.Get());
	}
}
